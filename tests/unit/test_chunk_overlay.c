// Light-emitting blocks are re-sent as block updates after each chunk
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "check.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "packets.h"
#include "net.h"

static void resetChanges (uint32_t capacity) {
  block_changes_cap = capacity;
  block_changes = malloc(capacity * sizeof(BlockChange));
  for (uint32_t i = 0; i < capacity; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  CHECK(bc_index_init(capacity) == 0);
  memset(player_data, 0, sizeof(player_data));
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
}

// Counts the Block Update packets (0x08) a client receives for one chunk
static int countBlockUpdates (int chunk_x, int chunk_z) {
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
  fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
  CHECK(net_open(fds[0]) == 0);
  sc_chunkDataAndUpdateLight(fds[0], chunk_x, chunk_z);

  static uint8_t stream[1 << 20];
  size_t got = 0;
  for (int i = 0; i < 10000 && (net_pending(fds[0]) || i == 0); i ++) {
    net_flush(fds[0]);
    ssize_t n;
    while ((n = recv(fds[1], stream + got, sizeof(stream) - got, 0)) > 0) got += n;
  }

  int updates = 0;
  size_t pos = 0;
  while (pos < got) {
    uint32_t length = 0;
    int shift = 0;
    while (stream[pos] & 0x80) length |= (uint32_t)(stream[pos ++] & 0x7F) << shift, shift += 7;
    length |= (uint32_t)stream[pos ++] << shift;
    if (stream[pos] == 0x08) updates ++;
    pos += length;
  }
  net_close(fds[0]);
  close(fds[0]);
  close(fds[1]);
  return updates;
}

static void test_torches_and_chests_only (void) {
  resetChanges(1000);
  CHECK(makeBlockChange(3, 120, 3, B_torch) == 0);
  CHECK(makeBlockChange(5, 120, 5, B_chest) == 0);
  CHECK(makeBlockChange(40, 120, 3, B_torch) == 0);   // another chunk
  // Chest item data whose bytes look like a torch inside this chunk
  int chest = bc_find(5, 120, 5);
  block_changes[chest + 1] = (BlockChange){ .x = 2, .z = 2, .y = 100, .block = B_torch };
  CHECK(countBlockUpdates(0, 0) == 2);
}

int main (void) {
  platform = bareiron_posix_platform();
  RUN(test_torches_and_chests_only);
  return CHECK_DONE();
}
