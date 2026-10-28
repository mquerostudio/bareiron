// A chunk is built and queued without giving the CPU away midway: on the
// ESP32-P4 every yield is a vTaskDelay(1), up to 1 ms each
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>

#include "check.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "net.h"
#include "packets.h"

static int yields = 0;
static void counting_yield (void) { yields ++; }

static void test_chunk_has_no_yields (void) {
  static BareironPlatform counting;
  counting = *bareiron_posix_platform();
  counting.yield = counting_yield;
  platform = &counting;
  block_changes_cap = 100;
  block_changes = calloc(block_changes_cap, sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  CHECK(bc_index_init(block_changes_cap) == 0);

  int sv[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  CHECK(net_open(sv[0]) == 0);
  yields = 0;
  sc_chunkDataAndUpdateLight(sv[0], 3, -2);
  printf("  %d yields for one chunk\n", yields);
  CHECK(yields == 0);
  net_close(sv[0]);
  close(sv[0]);
  close(sv[1]);
}

int main (void) {
  RUN(test_chunk_has_no_yields);
  return CHECK_DONE();
}
