#include <string.h>

#include "globals.h"
#include "tools.h"
#include "status.h"

static BareironStatus status;
// When each remembered player entered the game, by player_data index
static int64_t joined_us[MAX_PLAYERS];

void statusReset (void) {
  memset(&status, 0, sizeof(status));
  memset(joined_us, 0, sizeof(joined_us));
}

void statusEmit (BareironEventType type, const char *name, int code) {
  if (!platform->on_event) return;
  BareironEvent event = { .type = type, .code = code };
  if (name) strncpy(event.name, name, sizeof(event.name) - 1);
  platform->on_event(&event);
}

int statusPlayersOnline (void) {
  int online = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == -1) continue;
    if (player_data[i].flags & 0x20) continue;
    online ++;
  }
  return online;
}

// Rebuilds the snapshot from player_data; the caller holds the lock
static void fillPlayers (void) {
  status.player_count = 0;
  for (int i = 0; i < MAX_PLAYERS && status.player_count < MAX_CONNECTIONS; i ++) {
    PlayerData *player = &player_data[i];
    if (player->client_fd == -1) continue;
    if (player->flags & 0x20) continue;
    BareironPlayerInfo *info = &status.players[status.player_count ++];
    memcpy(info->name, player->name, sizeof(info->name));
    info->name[sizeof(info->name) - 1] = '\0';
    info->x = player->x;
    info->y = player->y;
    info->z = player->z;
    info->joined_us = joined_us[i];
  }
}

void statusSetRunning (uint8_t running) {
  platform->lock();
  status.running = running;
  if (running) status.started_us = get_program_time();
  else status.player_count = 0;
  platform->unlock();
}

void statusPlayerJoined (PlayerData *player) {
  joined_us[player - player_data] = get_program_time();
  platform->lock();
  fillPlayers();
  platform->unlock();
  statusEmit(BAREIRON_EVT_PLAYER_JOIN, player->name, 0);
}

void statusPlayerLeft (PlayerData *player) {
  platform->lock();
  fillPlayers();
  platform->unlock();
  statusEmit(BAREIRON_EVT_PLAYER_LEAVE, player->name, 0);
}

void statusUpdate (uint32_t loop_max_us) {
  uint32_t used = 0;
  for (int i = 0; i < block_changes_count; i ++) {
    if (block_changes[i].block != 0xFF) used ++;
  }
  platform->lock();
  fillPlayers();
  status.world_time = world_time;
  status.loop_max_us = loop_max_us;
  status.block_changes_used = used;
  platform->unlock();
}

void bareiron_get_status (BareironStatus *out) {
  if (!platform) {
    memset(out, 0, sizeof(*out));
    return;
  }
  platform->lock();
  *out = status;
  platform->unlock();
}
