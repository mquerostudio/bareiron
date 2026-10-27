// Standalone server for PC platforms (macOS, Linux, Windows)
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#ifdef _WIN32
  #include <winsock2.h>
#endif

#include "bareiron.h"
#include "globals.h"

static void handleSignal (int signal) {
  (void)signal;
  bareiron_request_stop();
}

int main () {
  #ifdef _WIN32 //initialize windows socket
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
      fprintf(stderr, "WSAStartup failed\n");
      return EXIT_FAILURE;
    }
  #endif

  // Line-buffered output, so that logs show up immediately when piped
  setvbuf(stdout, NULL, _IOLBF, 0);

  BareironConfig config = {
    .port = PORT,
    .world_seed = INITIAL_WORLD_SEED,
    .max_block_changes = MAX_BLOCK_CHANGES,
    .platform = bareiron_posix_platform()
  };
  // Tests run several servers side by side on different ports
  const char *port = getenv("BAREIRON_PORT");
  if (port) config.port = atoi(port);

  signal(SIGINT, handleSignal);
  signal(SIGTERM, handleSignal);

  int result = bareiron_run(&config);

  #ifdef _WIN32 //cleanup windows socket
    WSACleanup();
  #endif

  return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
