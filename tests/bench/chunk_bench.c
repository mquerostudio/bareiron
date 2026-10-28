// Where a chunk's time goes: generation, framing+compression, yields
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include "bareiron.h"
#include "globals.h"
#include "net.h"
#include "packets.h"
#include "worldgen.h"
#include "blockindex.h"

static int yields = 0;
static void count_yield (void) { yields ++; }
static int64_t ns (void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec*1000000000LL + t.tv_nsec; }

int main (void) {
  static BareironPlatform p;
  p = *bareiron_posix_platform();
  p.yield = count_yield;
  platform = &p;
  world_seed = 0x432B506Fu;
  block_changes_cap = 100; block_changes = calloc(100, sizeof(BlockChange));
  for (int i = 0; i < 100; i ++) block_changes[i].block = 0xFF;
  bc_index_init(100);

  // 1. generation only: 25 chunks x 20 sections
  int64_t t0 = ns();
  for (int c = 0; c < 25; c ++) for (int s = 0; s < 20; s ++) buildChunkSection((c % 5) * 16, s * 16, (c / 5) * 16);
  int64_t gen = ns() - t0;
  // only the 8 lowest sections (y < 128)
  t0 = ns();
  for (int c = 0; c < 25; c ++) for (int s = 0; s < 8; s ++) buildChunkSection((c % 5) * 16 + 160, s * 16, (c / 5) * 16);
  int64_t gen_low = ns() - t0;

  // 2. the whole packet, compressed, into a socketpair
  int sv[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
  net_open(sv[0]);
  net_enable_compression(sv[0], 256);
  yields = 0;
  t0 = ns();
  for (int c = 0; c < 25; c ++) {
    sc_chunkDataAndUpdateLight(sv[0], (c % 5) + 20, c / 5);
    net_flush(sv[0]);
    char sink[65536]; while (read(sv[1], sink, sizeof(sink)) == (ssize_t)sizeof(sink)) {}
  }
  int64_t full = ns() - t0;
  printf("per chunk: generate 20 sections %.2f ms, generate 8 low sections %.2f ms, full packet compressed %.2f ms, yields %d per chunk\n",
         gen / 25e6, gen_low / 25e6, full / 25e6, yields / 25);
  printf("so framing+compression ~ %.2f ms per chunk on this Mac\n", (full - gen) / 25e6);
  return 0;
}
