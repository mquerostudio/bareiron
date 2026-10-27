// World snapshots: header, block changes and player data
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "check.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "serialize.h"

static void resetWorld (void) {
  if (block_changes) free(block_changes);
  block_changes_cap = 2000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  memset(player_data, 0, sizeof(player_data));
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  player_data_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
}

static void buildWorld (void) {
  resetWorld();
  world_seed = 0x12345678;
  world_time = 4321;
  for (int i = 0; i < 500; i ++) makeBlockChange(i % 50, 100 + i / 50, 7, B_stone);
  makeBlockChange(3, 120, 3, B_chest);
  for (int i = 0; i < 3; i ++) {
    player_data[i].uuid[0] = i + 1;
    snprintf(player_data[i].name, 16, "player%d", i);
    player_data[i].client_fd = 40 + i;
    player_data[i].x = 100 * i;
    player_data[i].inventory_items[0] = 7 + i;
    player_data[i].inventory_count[0] = 3;
  }
  player_data_count = 3;
}

static void test_snapshot_roundtrip (void) {
  buildWorld();
  size_t size = worldSnapshotSize();
  uint8_t *buffer = malloc(size);
  CHECK(worldSnapshotWrite(buffer, size) == size);
  // Connected players are saved as offline, the live data is untouched
  CHECK(player_data[1].client_fd == 41);

  resetWorld();
  world_seed = 0;
  world_time = 0;
  CHECK(worldSnapshotRead(buffer, size) == 0);
  bc_index_rebuild();

  CHECK(world_seed == 0x12345678);
  CHECK(world_time == 4321);
  CHECK(getBlockChange(10, 100, 7) == B_stone);
  CHECK(getBlockChange(49, 109, 7) == B_stone);
  CHECK(getBlockChange(3, 120, 3) == B_chest);
  CHECK(player_data_count == 3);
  CHECK(strcmp(player_data[2].name, "player2") == 0);
  CHECK(player_data[2].x == 200);
  CHECK(player_data[1].inventory_items[0] == 8 && player_data[1].inventory_count[0] == 3);
  CHECK(player_data[0].client_fd == -1);
  free(buffer);
}

static void test_bad_magic_rejected (void) {
  buildWorld();
  size_t size = worldSnapshotSize();
  uint8_t *buffer = malloc(size);
  worldSnapshotWrite(buffer, size);
  buffer[0] ^= 0xFF;
  CHECK(worldSnapshotRead(buffer, size) == -5);
  free(buffer);
}

static void test_truncated_rejected (void) {
  buildWorld();
  size_t size = worldSnapshotSize();
  uint8_t *buffer = malloc(size);
  worldSnapshotWrite(buffer, size);
  CHECK(worldSnapshotRead(buffer, size - 10) == -5);
  CHECK(worldSnapshotRead(buffer, 4) == -5);
  free(buffer);
}

// Players move, eat and pick things up all the time; while anyone is
// online the world counts as changed, so the 60 s save keeps them
static void test_online_player_marks_world_dirty (void) {
  buildWorld();
  player_data[0].flags = 0;
  saveWorldIfDue(); // nothing to save yet
  CHECK(!worldIsDirty());
  handleServerTick(1000000);
  CHECK(worldIsDirty());
}

int main (void) {
  // Saves land in the working directory; keep them out of the repository
  char dir[] = "/tmp/bareiron-test-XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  CHECK(chdir(dir) == 0);
  platform = bareiron_posix_platform();
  RUN(test_snapshot_roundtrip);
  RUN(test_bad_magic_rejected);
  RUN(test_truncated_rejected);
  RUN(test_online_player_marks_world_dirty);
  return CHECK_DONE();
}
