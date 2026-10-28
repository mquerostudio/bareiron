// Where a new player appears: on dry land, never under water or ice
#include <stdlib.h>
#include <time.h>

#include "check.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "worldgen.h"

static int64_t now_ns (void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void setup (void) {
  block_changes_cap = 100;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
}

static uint8_t isDry (short x, short z) {
  uint8_t h = getHeightAt(x, z);
  uint8_t ground = getBlockAt(x, h, z);
  return
    !isPassableBlock(ground) && ground != B_ice &&
    isPassableSpawnBlock(getBlockAt(x, h + 1, z)) &&
    isPassableSpawnBlock(getBlockAt(x, h + 2, z));
}

static void test_spawn_is_on_dry_land (void) {
  int wet_default = 0;
  for (uint32_t i = 0; i < 30; i ++) {
    world_seed = 0x9E3779B9u * (i + 1);
    if (!isDry(8, 8)) wet_default ++;
    short x, z;
    findSpawnColumn(&x, &z);
    CHECK(isDry(x, z));
  }
  printf("  %d of 30 seeds put (8, 8) in water or on ice\n", wet_default);
  CHECK(wet_default > 0);   // the test must cover the case that bit Manuel
}

// A world that is all ocean near the origin must not make the search run on
static void test_spawn_search_is_bounded (void) {
  world_seed = 0x9E3779B9u;
  const int lookups = 200;
  int64_t start = now_ns();
  volatile uint8_t sink = 0;
  for (int i = 0; i < lookups; i ++) sink ^= isDry((short)(i * 37), (short)(i * 53));
  int64_t one_column = (now_ns() - start) / lookups;

  start = now_ns();
  short x, z;
  for (int i = 0; i < 10; i ++) findSpawnColumn(&x, &z);
  int64_t per_search = (now_ns() - start) / 10;
  (void)sink;
  CHECK(per_search < (64 * 64 / 16 + 32) * one_column * 2);
}

int main (void) {
  platform = bareiron_posix_platform();
  setup();
  RUN(test_spawn_is_on_dry_land);
  RUN(test_spawn_search_is_bounded);
  return CHECK_DONE();
}
