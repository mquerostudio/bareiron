#ifndef H_BAREIRON
#define H_BAREIRON

// Public API for embedding bareiron in a host program (a PC binary, or a
// firmware such as KodeCraft on the Kode Dot). Everything that depends on
// the platform reaches the server through BareironPlatform.

#include <stddef.h>
#include <stdint.h>

typedef enum {
  BAREIRON_EVT_READY,
  BAREIRON_EVT_PLAYER_JOIN,
  BAREIRON_EVT_PLAYER_LEAVE,
  BAREIRON_EVT_WORLD_SAVED,
  BAREIRON_EVT_STOPPED,
  BAREIRON_EVT_ERROR
} BareironEventType;

typedef struct {
  BareironEventType type;
  char name[16];
  int code;
} BareironEvent;

typedef struct {
  void   *(*alloc_large) (size_t size);   // big buffers (PSRAM on the Dot); NULL on failure
  void    (*free_large) (void *ptr);
  void    (*lock) (void);                  // guards the status snapshot
  void    (*unlock) (void);
  int64_t (*now_us) (void);                // monotonic microseconds
  void    (*yield) (void);                 // called once per loop iteration
  void    (*on_event) (const BareironEvent *event); // may be NULL
  // Takes ownership of `data` (allocated with alloc_large): write it now
  // or hand it to another task, and free_large it once written. Called on
  // the server thread, so it should return quickly. 0 = accepted.
  int     (*save_world) (void *data, size_t len);
  int     (*load_world) (void *data, size_t capacity, size_t *len); // 0 = ok, 1 = no world yet, <0 = error
} BareironPlatform;

typedef struct {
  uint16_t port;               // 25565
  uint32_t world_seed;         // used only when there is no saved world
  uint32_t max_block_changes;  // capacity of block_changes
  const BareironPlatform *platform;
} BareironConfig;

// Most clients that can be connected at once
#define BAREIRON_MAX_CONNECTIONS 8

typedef struct {
  char name[16];
  short x;
  uint8_t y;
  short z;
  int64_t joined_us;
} BareironPlayerInfo;

typedef struct {
  uint8_t  running;
  uint8_t  player_count;                         // players in game (not status pings)
  BareironPlayerInfo players[BAREIRON_MAX_CONNECTIONS];
  uint16_t world_time;                           // 0..23999
  int64_t  started_us;
  uint32_t loop_max_us;                          // worst loop iteration in the last second
  uint32_t block_changes_used;
} BareironStatus;

// Error codes returned by bareiron_run
#define BAREIRON_ERR_SOCKET -1
#define BAREIRON_ERR_BIND -2
#define BAREIRON_ERR_LISTEN -3
#define BAREIRON_ERR_NO_MEMORY -4
#define BAREIRON_ERR_BAD_WORLD -5

// Runs the server on the calling thread until bareiron_request_stop is
// called. Returns 0 after a requested stop, or a negative error code if
// the server could not start.
int bareiron_run (const BareironConfig *config);

// Asks a running server to stop. Safe to call from any thread; the
// server returns from bareiron_run within a second.
void bareiron_request_stop (void);

// Copies the latest server status. Safe to call from any thread.
void bareiron_get_status (BareironStatus *out);

// Platform implementation for POSIX systems (macOS, Linux)
const BareironPlatform *bareiron_posix_platform (void);

#endif
