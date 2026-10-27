// Starts bareiron with a minimal ESP-IDF platform, without any network.
// Only meant to prove that the component compiles and links.
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "bareiron.h"

static SemaphoreHandle_t status_lock;

static void *alloc_large (size_t size) { return malloc(size); }
static void free_large (void *ptr) { free(ptr); }
static void lock (void) { xSemaphoreTake(status_lock, portMAX_DELAY); }
static void unlock (void) { xSemaphoreGive(status_lock); }
static int64_t now_us (void) { return esp_timer_get_time(); }
static void yield (void) { vTaskDelay(1); }
static int save_world (void *data, size_t len) { (void)len; free(data); return 0; }
static int load_world (void *data, size_t capacity, size_t *len) { (void)data; (void)capacity; (void)len; return 1; }

static const BareironPlatform platform = {
  alloc_large, free_large, lock, unlock, now_us, yield, NULL, save_world, load_world
};

void app_main (void) {
  status_lock = xSemaphoreCreateMutex();
  BareironConfig config = {
    .port = 25565,
    .world_seed = 0xA103DE6C,
    .max_block_changes = 20000,
    .platform = &platform
  };
  bareiron_run(&config);
}
