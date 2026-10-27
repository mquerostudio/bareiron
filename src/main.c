// Standalone server for PC platforms (macOS, Linux)
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include "bareiron.h"
#include "globals.h"

static volatile sig_atomic_t status_requested = 0;
static volatile int server_done = 0;
static BareironConfig config;
static int run_result;

static void handleStop (int signal) {
  (void)signal;
  bareiron_request_stop();
}

static void handleStatusRequest (int signal) {
  (void)signal;
  status_requested = 1;
}

static const char *eventName (BareironEventType type) {
  switch (type) {
    case BAREIRON_EVT_READY: return "ready";
    case BAREIRON_EVT_PLAYER_JOIN: return "join";
    case BAREIRON_EVT_PLAYER_LEAVE: return "leave";
    case BAREIRON_EVT_WORLD_SAVED: return "world_saved";
    case BAREIRON_EVT_STOPPED: return "stopped";
    default: return "error";
  }
}

// One line per event, so tests and scripts can follow the server
static void printEvent (const BareironEvent *event) {
  printf("EVENT {\"type\":\"%s\",\"name\":\"%s\",\"code\":%d}\n", eventName(event->type), event->name, event->code);
}

static void printStatus (void) {
  BareironStatus status;
  bareiron_get_status(&status);
  printf("STATUS {\"running\":%s,\"player_count\":%d,\"world_time\":%d,\"loop_max_us\":%u,\"block_changes_used\":%u,\"players\":[",
    status.running ? "true" : "false", status.player_count, status.world_time,
    status.loop_max_us, status.block_changes_used);
  for (int i = 0; i < status.player_count; i ++) {
    BareironPlayerInfo *player = &status.players[i];
    printf("%s{\"name\":\"%s\",\"x\":%d,\"y\":%d,\"z\":%d}", i ? "," : "", player->name, player->x, player->y, player->z);
  }
  printf("]}\n");
}

static void *serverThread (void *arg) {
  (void)arg;
  run_result = bareiron_run(&config);
  server_done = 1;
  return NULL;
}

int main () {
  // Line-buffered output, so that logs show up immediately when piped
  setvbuf(stdout, NULL, _IOLBF, 0);

  static BareironPlatform platform_with_events;
  platform_with_events = *bareiron_posix_platform();
  platform_with_events.on_event = printEvent;

  config = (BareironConfig){
    .port = PORT,
    .world_seed = INITIAL_WORLD_SEED,
    .max_block_changes = MAX_BLOCK_CHANGES,
    .platform = &platform_with_events
  };
  // Tests run servers on other ports
  const char *port = getenv("BAREIRON_PORT");
  if (port) config.port = atoi(port);

  signal(SIGINT, handleStop);
  signal(SIGTERM, handleStop);
  signal(SIGUSR1, handleStatusRequest);
  signal(SIGPIPE, SIG_IGN);

  // The server runs on its own thread; this one answers status requests
  pthread_t thread;
  pthread_create(&thread, NULL, serverThread, NULL);
  struct timespec pause = { 0, 20 * 1000 * 1000 };
  while (!server_done) {
    if (status_requested) {
      status_requested = 0;
      printStatus();
    }
    nanosleep(&pause, NULL);
  }
  pthread_join(thread, NULL);

  return run_result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
