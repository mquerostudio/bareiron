// Lifecycle: bareiron_run must return on request and be restartable
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <stdio.h>

#include "check.h"
#include "bareiron.h"

static BareironConfig config;
static int run_result;

static int64_t now_ms (void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void *run_thread (void *arg) {
  (void)arg;
  run_result = bareiron_run(&config);
  return NULL;
}

// Starts the server, requests a stop after 200 ms and returns elapsed ms
static int64_t run_then_stop (void) {
  pthread_t thread;
  run_result = 12345;
  pthread_create(&thread, NULL, run_thread, NULL);
  usleep(200 * 1000);
  int64_t asked = now_ms();
  bareiron_request_stop();
  pthread_join(thread, NULL);
  return now_ms() - asked;
}

static void test_run_and_stop (void) {
  config.port = 25599;
  CHECK(run_then_stop() < 1500);
  CHECK(run_result == 0);
  // The port must be released so a second run can bind again
  CHECK(run_then_stop() < 1500);
  CHECK(run_result == 0);
}

// A stop asked for just before the server task starts must not be lost:
// the app has already shown "Parando" and waits for STOPPED
static void test_stop_before_run_is_kept (void) {
  config.port = 25597;
  bareiron_request_stop();
  pthread_t thread;
  run_result = 12345;
  int64_t start = now_ms();
  pthread_create(&thread, NULL, run_thread, NULL);
  // Without the fix the server would run until someone stops it again
  for (int i = 0; i < 30 && run_result == 12345; i ++) usleep(100 * 1000);
  if (run_result == 12345) {
    bareiron_request_stop();
    pthread_join(thread, NULL);
    CHECK(!"bareiron_run ignored a stop requested before it started");
    return;
  }
  pthread_join(thread, NULL);
  CHECK(run_result == 0);
  CHECK(now_ms() - start < 3000);
  // And the next run is a normal one
  CHECK(run_then_stop() < 1500);
  CHECK(run_result == 0);
}

static void test_bind_error_returns (void) {
  int blocker = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(25598), .sin_addr.s_addr = INADDR_ANY };
  CHECK(bind(blocker, (struct sockaddr *)&addr, sizeof(addr)) == 0);
  CHECK(listen(blocker, 1) == 0);
  config.port = 25598;
  CHECK(bareiron_run(&config) == -2);
  close(blocker);
}

// A world file that can't be read must stop the start, and stay as it was
static void test_bad_world_is_kept (void) {
  FILE *file = fopen("world.bin", "wb");
  fputs("not a world", file);
  fclose(file);
  config.port = 25597;
  CHECK(bareiron_run(&config) == -5);
  char buffer[32] = { 0 };
  file = fopen("world.bin", "rb");
  CHECK(fread(buffer, 1, sizeof(buffer), file) == 11);
  fclose(file);
  CHECK(strcmp(buffer, "not a world") == 0);
  remove("world.bin");
}

int main (void) {
  // Keep any world file away from the repository
  char dir[] = "/tmp/bareiron-test-XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  CHECK(chdir(dir) == 0);

  memset(&config, 0, sizeof(config));
  config.world_seed = 0xA103DE6C;
  config.max_block_changes = 20000;
  config.platform = bareiron_posix_platform();

  RUN(test_run_and_stop);
  RUN(test_stop_before_run_is_kept);
  RUN(test_bind_error_returns);
  RUN(test_bad_world_is_kept);
  return CHECK_DONE();
}
