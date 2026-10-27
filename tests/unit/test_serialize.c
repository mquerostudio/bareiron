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

// A world file comes from an SD card: whatever it says, loading it must
// not leave values that index out of bounds or point at stale memory
static void test_loaded_players_are_sanitized (void) {
  buildWorld();
  player_data[1].hotbar = 200;
  player_data[1].flags = 0x80 | 0x10 | 0x20 | 0x01 | 0x02;
  memset(player_data[1].craft_items, 0xAB, sizeof(player_data[1].craft_items));
  memset(player_data[1].name, 'A', 16);
  size_t size = worldSnapshotSize();
  uint8_t *buffer = malloc(size);
  worldSnapshotWrite(buffer, size);
  resetWorld();
  CHECK(worldSnapshotRead(buffer, size) == 0);
  CHECK(player_data[1].hotbar < 9);
  CHECK((player_data[1].flags & (0x01 | 0x04 | 0x08 | 0x10 | 0x20 | 0x40 | 0x80)) == 0);
  CHECK(player_data[1].flags & 0x02);
  CHECK(player_data[1].craft_items[0] == 0 && player_data[1].craft_items[8] == 0);
  CHECK(player_data[1].name[15] == '\0');
  free(buffer);
}

static void test_chest_overrunning_count_rejected (void) {
  resetWorld();
  for (int i = 0; i < 4; i ++) makeBlockChange(i, 120, 0, B_stone);
  makeBlockChange(10, 120, 0, B_chest);   // entries 4..18
  size_t size = worldSnapshotSize();
  uint8_t *buffer = malloc(size);
  worldSnapshotWrite(buffer, size);
  // Cut the block changes right after the chest itself
  WorldHeader header;
  memcpy(&header, buffer, sizeof(header));
  uint32_t full = header.block_changes_count;
  header.block_changes_count = 5;
  memcpy(buffer, &header, sizeof(header));
  size_t players = MAX_PLAYERS * sizeof(PlayerData);
  memmove(buffer + sizeof(header) + 5 * sizeof(BlockChange),
          buffer + sizeof(header) + full * sizeof(BlockChange), players);
  CHECK(worldSnapshotRead(buffer, sizeof(header) + 5 * sizeof(BlockChange) + players) == -5);
  free(buffer);
}

// The platform may hand the snapshot to another task and write it later:
// the buffer is the platform's after save_world, to free when it's done
static void *saved_data = NULL;
static size_t saved_len = 0;
static int deferredSave (void *data, size_t len) {
  saved_data = data;
  saved_len = len;
  return 0;
}

static void test_save_hands_buffer_to_platform (void) {
  buildWorld();
  BareironPlatform deferred = *bareiron_posix_platform();
  deferred.save_world = deferredSave;
  platform = &deferred;
  CHECK(saveWorldNow() == 0);
  CHECK(saved_data != NULL && saved_len == worldSnapshotSize());
  // Still readable after saveWorldNow returned
  WorldHeader header;
  memcpy(&header, saved_data, sizeof(header));
  CHECK(header.magic == WORLD_MAGIC);
  platform->free_large(saved_data);
  platform = bareiron_posix_platform();
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
  RUN(test_loaded_players_are_sanitized);
  RUN(test_chest_overrunning_count_rejected);
  RUN(test_save_hands_buffer_to_platform);
  return CHECK_DONE();
}
