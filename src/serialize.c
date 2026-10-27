#include <stdio.h>
#include <string.h>

#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "status.h"
#include "serialize.h"

static uint8_t world_dirty = false;
static int64_t last_save_time = 0;

void markWorldDirty (void) {
  world_dirty = true;
}

uint8_t worldIsDirty (void) {
  return world_dirty;
}

size_t worldSnapshotSize (void) {
  return sizeof(WorldHeader)
    + (size_t)block_changes_count * sizeof(BlockChange)
    + MAX_PLAYERS * sizeof(PlayerData);
}

size_t worldSnapshotWrite (uint8_t *dst, size_t capacity) {
  size_t size = worldSnapshotSize();
  if (capacity < size) return 0;

  WorldHeader header = {
    .magic = WORLD_MAGIC,
    .version = WORLD_VERSION,
    .world_seed = world_seed,
    .block_changes_count = block_changes_count,
    .player_count = MAX_PLAYERS,
    .player_size = sizeof(PlayerData),
    .world_time = world_time
  };
  memcpy(dst, &header, sizeof(header));
  dst += sizeof(header);

  memcpy(dst, block_changes, (size_t)block_changes_count * sizeof(BlockChange));
  dst += (size_t)block_changes_count * sizeof(BlockChange);

  // Everyone is offline in a saved world
  memcpy(dst, player_data, sizeof(player_data));
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    int offline = -1;
    memcpy(dst + i * sizeof(PlayerData) + offsetof(PlayerData, client_fd), &offline, sizeof(offline));
  }

  return size;
}

int worldSnapshotRead (const uint8_t *src, size_t len) {
  WorldHeader header;
  if (len < sizeof(header)) return BAREIRON_ERR_BAD_WORLD;
  memcpy(&header, src, sizeof(header));

  if (header.magic != WORLD_MAGIC || header.version != WORLD_VERSION) return BAREIRON_ERR_BAD_WORLD;
  if (header.player_size != sizeof(PlayerData) || header.player_count > MAX_PLAYERS) return BAREIRON_ERR_BAD_WORLD;
  if (header.block_changes_count > block_changes_cap) return BAREIRON_ERR_BAD_WORLD;
  size_t expected = sizeof(header)
    + (size_t)header.block_changes_count * sizeof(BlockChange)
    + (size_t)header.player_count * sizeof(PlayerData);
  if (len != expected) return BAREIRON_ERR_BAD_WORLD;
  src += sizeof(header);

  // Every chest needs its 14 entries of item data inside the file
  #ifdef ALLOW_CHESTS
  const BlockChange *changes = (const BlockChange *)src;
  for (uint32_t i = 0; i < header.block_changes_count; i ++) {
    if (changes[i].block != B_chest) continue;
    if (i + 14 >= header.block_changes_count) return BAREIRON_ERR_BAD_WORLD;
    i += 14;
  }
  #endif

  world_seed = header.world_seed;
  world_time = header.world_time;

  block_changes_count = header.block_changes_count;
  memcpy(block_changes, src, (size_t)block_changes_count * sizeof(BlockChange));
  for (uint32_t i = block_changes_count; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  src += (size_t)block_changes_count * sizeof(BlockChange);

  memset(player_data, 0, sizeof(player_data));
  memcpy(player_data, src, (size_t)header.player_count * sizeof(PlayerData));
  player_data_count = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *player = &player_data[i];
    player->client_fd = -1;
    // Nothing from the file may index out of bounds or point anywhere:
    // drop the flags that only make sense while connected, and with them
    // any chest pointer kept in craft_items
    player->name[sizeof(player->name) - 1] = '\0';
    if (player->hotbar >= 9) player->hotbar = 0;
    if (player->flags & 0x80) memset(player->craft_items, 0, sizeof(player->craft_items));
    player->flags &= ~(0x01 | 0x04 | 0x08 | 0x10 | 0x20 | 0x40 | 0x80);
    for (int j = 0; j < 16; j ++) {
      if (player->uuid[j] == 0) continue;
      player_data_count ++;
      break;
    }
  }

  return 0;
}

int saveWorldNow (void) {
  size_t size = worldSnapshotSize();
  uint8_t *buffer = platform->alloc_large(size);
  if (!buffer) return BAREIRON_ERR_NO_MEMORY;
  worldSnapshotWrite(buffer, size);
  int result = platform->save_world(buffer, size);
  platform->free_large(buffer);

  last_save_time = get_program_time();
  if (result != 0) {
    printf("Failed to save the world (%d)\n", result);
    statusEmit(BAREIRON_EVT_ERROR, NULL, result);
    return result;
  }
  world_dirty = false;
  statusEmit(BAREIRON_EVT_WORLD_SAVED, NULL, (int)size);
  return 0;
}

void saveWorldIfDue (void) {
  if (!world_dirty) return;
  if (get_program_time() - last_save_time < WORLD_SAVE_INTERVAL) return;
  saveWorldNow();
}

int loadWorld (void) {
  world_dirty = false;
  last_save_time = get_program_time();

  size_t capacity = sizeof(WorldHeader)
    + (size_t)block_changes_cap * sizeof(BlockChange)
    + MAX_PLAYERS * sizeof(PlayerData);
  uint8_t *buffer = platform->alloc_large(capacity);
  if (!buffer) return BAREIRON_ERR_NO_MEMORY;

  size_t len = 0;
  int result = platform->load_world(buffer, capacity, &len);
  if (result == 0) result = worldSnapshotRead(buffer, len);
  else if (result < 0) result = BAREIRON_ERR_BAD_WORLD;
  platform->free_large(buffer);

  if (result == 0) {
    printf("World loaded: %d block changes, seed %08X\n\n", block_changes_count, (unsigned int)world_seed);
  } else if (result == 1) {
    printf("No saved world, starting a new one with seed %08X\n\n", (unsigned int)world_seed);
  } else {
    printf("The saved world can't be read (%d). Not starting, so it isn't overwritten.\n", result);
  }
  return result;
}
