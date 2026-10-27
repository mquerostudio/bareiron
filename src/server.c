#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifdef ESP_PLATFORM
  #include "lwip/sockets.h"
  #include "lwip/netdb.h"
#else
  #include <sys/types.h>
  #ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
  #else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
  #endif
  #include <unistd.h>
#endif

#include "bareiron.h"
#include "globals.h"
#include "tools.h"
#include "varnum.h"
#include "procedures.h"
#include "serialize.h"
#include "dispatch.h"
#include "net.h"

static volatile int stop_requested = 0;

void bareiron_request_stop (void) {
  stop_requested = 1;
}

// Puts every piece of global server state back to its initial value, so
// that the server can be started again after a stop
static void resetServerState (const BareironConfig *config) {
  world_seed = splitmix64(config->world_seed);
  rng_seed = splitmix64(INITIAL_RNG_SEED);
  world_time = 0;
  server_ticks = 0;
  client_count = 0;
  total_bytes_received = 0;
  recv_count = 0;

  block_changes_count = 0;
  for (uint32_t i = 0; i < block_changes_cap; i ++) {
    block_changes[i].block = 0xFF;
  }

  memset(player_data, 0, sizeof(player_data));
  player_data_count = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    player_data[i].client_fd = -1;
    client_states[i * 2] = -1;
    client_states[i * 2 + 1] = 0;
  }
  memset(mob_data, 0, sizeof(mob_data));
}

// Sends whatever each client has queued, and drops clients whose
// connection broke or who stopped reading (backlog over NET_TX_CAP)
static void flushClients (int *clients) {
  for (int i = 0; i < MAX_CONNECTIONS; i ++) {
    if (clients[i] == -1) continue;
    int result = net_flush(clients[i]);
    if (result == -2) disconnectClient(&clients[i], 9);
    else if (result < 0) disconnectClient(&clients[i], 10);
  }
}

static void setNonBlocking (int fd) {
  #ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
  #else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  #endif
}

// Creates the listening socket, returns its fd or a BAREIRON_ERR_* code
static int openServerSocket (uint16_t port) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    perror("socket failed");
    return BAREIRON_ERR_SOCKET;
  }

  int opt = 1;
  #ifdef _WIN32
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
  #else
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  #endif

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("bind failed");
    close(server_fd);
    return BAREIRON_ERR_BIND;
  }
  if (listen(server_fd, 5) < 0) {
    perror("listen failed");
    close(server_fd);
    return BAREIRON_ERR_LISTEN;
  }
  // Non-blocking, so that accept never stalls the loop
  setNonBlocking(server_fd);
  printf("Server listening on port %d...\n", port);
  return server_fd;
}

// How many packets to handle from one client before moving on to the next
#define PACKETS_PER_ITERATION 8

// Accepts one pending connection, if there is one and a slot is free
static void acceptClient (int server_fd, int *clients) {
  int slot = -1;
  for (int i = 0; i < MAX_CONNECTIONS && slot == -1; i ++) {
    if (clients[i] == -1) slot = i;
  }
  if (slot == -1) return;

  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);
  int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
  if (client_fd == -1) return;

  printf("New client, fd: %d\n", client_fd);
  setNonBlocking(client_fd);
  // Small packets (movement, keep-alive) shouldn't wait for Nagle
  int on = 1;
  setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
  #ifdef SO_NOSIGPIPE
  setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
  #endif
  if (net_open(client_fd)) {
    close(client_fd);
    return;
  }
  clients[slot] = client_fd;
  client_count ++;
}

// Reads what the client sent and handles every complete packet in it
static void handleClientPackets (int *client_slot) {
  int client_fd = *client_slot;

  if (net_pump(client_fd) < 0) {
    disconnectClient(client_slot, 1);
    return;
  }

  for (int n = 0; n < PACKETS_PER_ITERATION; n ++) {
    int length;
    int ready = net_next_packet(client_fd, &length);
    if (ready == 0) return;
    if (ready < 0) {
      disconnectClient(client_slot, 2);
      return;
    }

    int packet_id = readVarInt(client_fd);
    if (packet_id == VARNUM_ERROR) {
      disconnectClient(client_slot, 3);
      return;
    }
    int state = getClientState(client_fd);
    // Disconnect on legacy server list ping
    if (state == STATE_NONE && length == 254 && packet_id == 122) {
      disconnectClient(client_slot, 5);
      return;
    }

    handlePacket(client_fd, length - sizeVarInt(packet_id), packet_id, state);
    net_finish_packet(client_fd);

    // Handlers leave recv_count at 0 to ask for a disconnect
    if (recv_count <= 0) {
      disconnectClient(client_slot, 4);
      return;
    }
  }
}

int bareiron_run (const BareironConfig *config) {
  platform = config->platform;
  stop_requested = 0;

  block_changes_cap = config->max_block_changes;
  block_changes = platform->alloc_large(block_changes_cap * sizeof(BlockChange));
  if (!block_changes) return BAREIRON_ERR_NO_MEMORY;

  resetServerState(config);

  printf("World seed (hashed): %08X\n", (unsigned int)world_seed);
  printf("RNG seed (hashed): %08X\n\n", (unsigned int)rng_seed);

  // Start the disk/flash serializer (if applicable)
  if (initSerializer()) {
    platform->free_large(block_changes);
    block_changes = NULL;
    return BAREIRON_ERR_BAD_WORLD;
  }

  int server_fd = openServerSocket(config->port);
  if (server_fd < 0) {
    platform->free_large(block_changes);
    block_changes = NULL;
    return server_fd;
  }

  // Initialize all file descriptor references to -1 (unallocated)
  int clients[MAX_CONNECTIONS];
  for (int i = 0; i < MAX_CONNECTIONS; i ++) clients[i] = -1;

  // Track time of last server tick (in microseconds)
  int64_t last_tick_time = get_program_time();

  /**
   * Each iteration accepts at most one new connection, runs the server
   * tick when it's due, and handles up to PACKETS_PER_ITERATION complete
   * packets from every client. Nothing in here waits on the network.
   */
  while (!stop_requested) {
    task_yield();

    acceptClient(server_fd, clients);

    // Handle periodic events (server ticks), with or without players
    int64_t time_since_last_tick = get_program_time() - last_tick_time;
    if (time_since_last_tick > TIME_BETWEEN_TICKS) {
      handleServerTick(time_since_last_tick);
      last_tick_time = get_program_time();
    }

    for (int i = 0; i < MAX_CONNECTIONS; i ++) {
      if (clients[i] == -1) continue;
      handleClientPackets(&clients[i]);
    }

    // Send what's queued, including packets for clients that sent nothing
    flushClients(clients);
  }

  // Stop requested: drop every client, then release the port and memory
  flushClients(clients);
  for (int i = 0; i < MAX_CONNECTIONS; i ++) {
    if (clients[i] != -1) disconnectClient(&clients[i], 8);
  }
  writePlayerDataToDisk();
  close(server_fd);

  platform->free_large(block_changes);
  block_changes = NULL;

  printf("Server closed.\n");
  return 0;
}
