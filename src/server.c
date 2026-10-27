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
  int clients[MAX_CONNECTIONS], client_index = 0;
  for (int i = 0; i < MAX_CONNECTIONS; i ++) clients[i] = -1;

  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  // Track time of last server tick (in microseconds)
  int64_t last_tick_time = get_program_time();

  /**
   * Cycles through all connected clients, handling one packet at a time
   * from each player. With every iteration, attempts to accept a new
   * client connection.
   */
  while (!stop_requested) {
    task_yield();

    // Attempt to accept a new connection
    for (int i = 0; i < MAX_CONNECTIONS; i ++) {
      if (clients[i] != -1) continue;
      clients[i] = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
      // If the accept was successful, make the client non-blocking too
      if (clients[i] != -1) {
        printf("New client, fd: %d\n", clients[i]);
        setNonBlocking(clients[i]);
        // Small packets (movement, keep-alive) shouldn't wait for Nagle
        int nodelay = 1;
        setsockopt(clients[i], IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));
        #ifdef SO_NOSIGPIPE
        setsockopt(clients[i], SOL_SOCKET, SO_NOSIGPIPE, &nodelay, sizeof(nodelay));
        #endif
        if (net_open(clients[i])) {
          close(clients[i]);
          clients[i] = -1;
          break;
        }
        client_count ++;
      }
      break;
    }

    // Send what's queued, even to clients that aren't sending anything
    flushClients(clients);

    // Look for valid connected clients
    client_index ++;
    if (client_index == MAX_CONNECTIONS) client_index = 0;
    if (clients[client_index] == -1) continue;

    // Handle periodic events (server ticks)
    int64_t time_since_last_tick = get_program_time() - last_tick_time;
    if (time_since_last_tick > TIME_BETWEEN_TICKS) {
      handleServerTick(time_since_last_tick);
      last_tick_time = get_program_time();
      flushClients(clients);
      if (clients[client_index] == -1) continue;
    }

    // Handle this individual client
    int client_fd = clients[client_index];

    // Check if at least 2 bytes are available for reading
    #ifdef _WIN32
    recv_count = recv(client_fd, recv_buffer, 2, MSG_PEEK);
    if (recv_count == 0) {
      disconnectClient(&clients[client_index], 1);
      continue;
    }
    if (recv_count == SOCKET_ERROR) {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK) {
        continue; // no data yet, keep client alive
      } else {
        disconnectClient(&clients[client_index], 1);
        continue;
      }
    }
    #else
    recv_count = recv(client_fd, &recv_buffer, 2, MSG_PEEK);
    if (recv_count < 2) {
      if (recv_count == 0 || (recv_count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        disconnectClient(&clients[client_index], 1);
      }
      continue;
    }
    #endif

    // Read packet length
    int length = readVarInt(client_fd);
    if (length == VARNUM_ERROR) {
      disconnectClient(&clients[client_index], 2);
      continue;
    }
    // Read packet ID
    int packet_id = readVarInt(client_fd);
    if (packet_id == VARNUM_ERROR) {
      disconnectClient(&clients[client_index], 3);
      continue;
    }
    // Get client connection state
    int state = getClientState(client_fd);
    // Disconnect on legacy server list ping
    if (state == STATE_NONE && length == 254 && packet_id == 122) {
      disconnectClient(&clients[client_index], 5);
      continue;
    }
    // Handle packet data
    handlePacket(client_fd, length - sizeVarInt(packet_id), packet_id, state);
    if (recv_count == 0 || (recv_count == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
      disconnectClient(&clients[client_index], 4);
      continue;
    }
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
