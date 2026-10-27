#ifndef ESP_PLATFORM

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#include "bareiron.h"

static pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *posix_alloc_large (size_t size) { return malloc(size); }
static void posix_free_large (void *ptr) { free(ptr); }
static void posix_lock (void) { pthread_mutex_lock(&status_mutex); }
static void posix_unlock (void) { pthread_mutex_unlock(&status_mutex); }

static int64_t posix_now_us (void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

// Sleep briefly so an idle server doesn't spin a whole CPU core
static void posix_yield (void) {
  struct timespec ts = { 0, 200 * 1000 };
  nanosleep(&ts, NULL);
}

// Writes to a temporary file first, so a crash mid-write never
// leaves a truncated world behind
static int posix_save_world (const void *data, size_t len) {
  FILE *file = fopen("world.bin.tmp", "wb");
  if (!file) return -1;
  size_t written = fwrite(data, 1, len, file);
  if (fclose(file) != 0 || written != len) return -1;
  return rename("world.bin.tmp", "world.bin") == 0 ? 0 : -1;
}

static int posix_load_world (void *data, size_t capacity, size_t *len) {
  FILE *file = fopen("world.bin", "rb");
  if (!file) return 1;
  *len = fread(data, 1, capacity, file);
  int error = ferror(file);
  fclose(file);
  return error ? -1 : 0;
}

static const BareironPlatform posix_platform = {
  .alloc_large = posix_alloc_large,
  .free_large = posix_free_large,
  .lock = posix_lock,
  .unlock = posix_unlock,
  .now_us = posix_now_us,
  .yield = posix_yield,
  .on_event = NULL,
  .save_world = posix_save_world,
  .load_world = posix_load_world
};

const BareironPlatform *bareiron_posix_platform (void) {
  return &posix_platform;
}

#endif
