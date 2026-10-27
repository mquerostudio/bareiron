// Column index over block_changes
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "check.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "worldgen.h"

static void resetChanges (uint32_t capacity) {
  if (block_changes) free(block_changes);
  block_changes_cap = capacity;
  block_changes = malloc(capacity * sizeof(BlockChange));
  for (uint32_t i = 0; i < capacity; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(capacity) == 0);
}

// Stores a change at `index` and tells the index about it
static void put (int index, short x, uint8_t y, short z, uint8_t block) {
  block_changes[index] = (BlockChange){ x, z, y, block };
  if (index >= block_changes_count) block_changes_count = index + 1;
  bc_on_set(index);
}

static void test_find_after_set_and_clear (void) {
  resetChanges(100);
  put(0, 5, 64, -3, B_stone);
  CHECK(bc_find(5, 64, -3) == 0);
  CHECK(bc_find(5, 65, -3) == -1);
  bc_on_clear(0);
  block_changes[0].block = 0xFF;
  CHECK(bc_find(5, 64, -3) == -1);
}

static void test_column_lists_only_that_column (void) {
  resetChanges(100);
  put(0, 15, 64, 0, B_stone);
  put(1, 16, 64, 0, B_stone);
  put(2, -1, 64, 0, B_stone);
  const int *indices;
  CHECK(bc_column(0, 0, &indices) == 1 && indices[0] == 0);
  CHECK(bc_column(1, 0, &indices) == 1 && indices[0] == 1);
  CHECK(bc_column(-1, 0, &indices) == 1 && indices[0] == 2);
  CHECK(bc_column(5, 5, &indices) == 0);
}

static void test_chest_contents_not_indexed (void) {
  resetChanges(100);
  put(3, 1, 70, 1, B_chest);
  // Item data that happens to look like block entries at the same spot
  for (int i = 4; i < 18; i ++) block_changes[i] = (BlockChange){ 1, 1, 70, (uint8_t)i };
  block_changes_count = 18;
  CHECK(bc_find(1, 70, 1) == 3);
  const int *indices;
  CHECK(bc_column(0, 0, &indices) == 1);
  bc_index_rebuild();
  CHECK(bc_find(1, 70, 1) == 3);
  CHECK(bc_column(0, 0, &indices) == 1);
}

static void test_first_free_finds_run_of_15 (void) {
  resetChanges(100);
  put(0, 0, 64, 0, B_stone);
  put(5, 1, 64, 0, B_stone);     // gap of 4 at 1..4
  put(30, 2, 64, 0, B_stone);    // gap of 24 at 6..29
  CHECK(bc_first_free(1) == 1);
  CHECK(bc_first_free(15) == 6);
  // A chest's item data is never a free slot, whatever its bytes say
  resetChanges(40);
  put(0, 0, 64, 0, B_chest);
  for (int i = 1; i < 15; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 15;
  CHECK(bc_first_free(1) == 15);
  // Out of room
  CHECK(bc_first_free(30) == -1);
}

static void test_rebuild_matches_incremental (void) {
  resetChanges(5000);
  srand(7);
  for (int i = 0; i < 1000; i ++) {
    put(i, rand() % 200 - 100, 40 + rand() % 40, rand() % 200 - 100, B_stone);
  }
  int expected[1000];
  for (int i = 0; i < 1000; i ++) {
    expected[i] = bc_find(block_changes[i].x, block_changes[i].y, block_changes[i].z);
  }
  bc_index_rebuild();
  for (int i = 0; i < 1000; i ++) {
    CHECK(bc_find(block_changes[i].x, block_changes[i].y, block_changes[i].z) == expected[i]);
  }
}

static void test_200k_changes_lookup_fast (void) {
  resetChanges(200000);
  for (int i = 0; i < 200000; i ++) {
    put(i, (i % 1000) - 500, 64 + (i / 1000) % 64, (i / 64000) * 7, B_dirt);
  }
  struct timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  int found = 0;
  for (int i = 0; i < 10000; i ++) {
    int k = (i * 37) % 200000;
    if (bc_find(block_changes[k].x, block_changes[k].y, block_changes[k].z) >= 0) found ++;
  }
  clock_gettime(CLOCK_MONOTONIC, &b);
  int64_t us = (b.tv_sec - a.tv_sec) * 1000000 + (b.tv_nsec - a.tv_nsec) / 1000;
  printf("  10000 lookups in %lld us\n", (long long)us);
  CHECK(found == 10000);
  CHECK(us < 50000);
}

// makeBlockChange/getBlockChange go through the index end to end
static void test_make_block_change_roundtrip (void) {
  resetChanges(1000);
  uint8_t base = getBlockAt(10, 100, 10);
  CHECK(makeBlockChange(10, 100, 10, B_stone) == 0);
  CHECK(getBlockChange(10, 100, 10) == B_stone);
  CHECK(getBlockAt(10, 100, 10) == B_stone);
  // Replacing in place keeps one entry
  CHECK(makeBlockChange(10, 100, 10, B_dirt) == 0);
  CHECK(getBlockChange(10, 100, 10) == B_dirt);
  CHECK(block_changes_count == 1);
  // Restoring the terrain block removes the entry
  CHECK(makeBlockChange(10, 100, 10, base) == 0);
  CHECK(getBlockChange(10, 100, 10) == 0xFF);
  // The freed slot is reused
  CHECK(makeBlockChange(11, 100, 10, B_stone) == 0);
  CHECK(bc_find(11, 100, 10) == 0);
}

static void test_chest_takes_15_and_frees_them (void) {
  resetChanges(1000);
  CHECK(makeBlockChange(0, 100, 0, B_stone) == 0);
  CHECK(makeBlockChange(1, 100, 0, B_chest) == 0);
  CHECK(bc_find(1, 100, 0) == 1);
  CHECK(block_changes_count == 16);
  CHECK(makeBlockChange(2, 100, 0, B_stone) == 0);
  CHECK(bc_find(2, 100, 0) == 16);
  // Breaking the chest (y = 100 is open air) frees all 15 entries for reuse
  CHECK(makeBlockChange(1, 100, 0, B_air) == 0);
  CHECK(getBlockChange(1, 100, 0) == 0xFF);
  CHECK(bc_first_free(15) == 1);
}

static void test_full_capacity_fails_cleanly (void) {
  resetChanges(3);
  CHECK(makeBlockChange(0, 100, 0, B_stone) == 0);
  CHECK(makeBlockChange(1, 100, 0, B_stone) == 0);
  CHECK(makeBlockChange(2, 100, 0, B_stone) == 0);
  CHECK(makeBlockChange(3, 100, 0, B_stone) == 1);
  CHECK(getBlockChange(3, 100, 0) == 0xFF);
}

// One player has a chest open while another breaks it and builds there.
// The open chest UI must lose its pointer into block_changes, and even a
// corrupted entry must never make chunk building write out of bounds.
static void test_stale_chest_pointer_cannot_corrupt_chunks (void) {
  resetChanges(1000);
  memset(player_data, 0, sizeof(player_data));
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;

  CHECK(makeBlockChange(20, 120, -20, B_chest) == 0);
  int chest = bc_find(20, 120, -20);
  uint8_t *storage_ptr = (uint8_t *)&block_changes[chest + 1];
  memcpy(player_data[0].craft_items, &storage_ptr, sizeof(storage_ptr));
  player_data[0].flags |= 0x80;

  CHECK(makeBlockChange(20, 120, -20, B_stone) == 0);
  CHECK((player_data[0].flags & 0x80) == 0);
  uint8_t *left = NULL;
  memcpy(&left, player_data[0].craft_items, sizeof(left));
  CHECK(left == NULL);

  CHECK(makeBlockChange(21, 120, -20, B_stone) == 0);
  CHECK(makeBlockChange(22, 120, -20, B_stone) == 0);
  // What a stale UI (or a forged packet) used to do: write an item into
  // the bytes that now hold a block entry
  storage_ptr[0] = 1; storage_ptr[1] = 0; storage_ptr[2] = 1;
  buildChunkSection(16, 112, -32);
}

int main (void) {
  platform = bareiron_posix_platform();
  RUN(test_find_after_set_and_clear);
  RUN(test_column_lists_only_that_column);
  RUN(test_chest_contents_not_indexed);
  RUN(test_first_free_finds_run_of_15);
  RUN(test_rebuild_matches_incremental);
  RUN(test_200k_changes_lookup_fast);
  RUN(test_make_block_change_roundtrip);
  RUN(test_chest_takes_15_and_frees_them);
  RUN(test_full_capacity_fails_cleanly);
  RUN(test_stale_chest_pointer_cannot_corrupt_chunks);
  return CHECK_DONE();
}
