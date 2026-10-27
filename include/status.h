#ifndef H_STATUS
#define H_STATUS

#include "globals.h"

// Keeps the snapshot that bareiron_get_status hands to the host, and
// sends events to the host through platform->on_event

void statusReset (void);
void statusEmit (BareironEventType type, const char *name, int code);
void statusPlayerJoined (PlayerData *player);
void statusPlayerLeft (PlayerData *player);
void statusSetRunning (uint8_t running);
// Refreshes positions, world time and counters; call about once a second
void statusUpdate (uint32_t loop_max_us);
// Players in the game right now, not counting status pings or logins
int  statusPlayersOnline (void);

#endif
