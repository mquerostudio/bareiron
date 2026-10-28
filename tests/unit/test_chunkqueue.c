// Chunks waiting to be sent, a few per loop iteration
#include <string.h>

#include "check.h"
#include "globals.h"
#include "chunkqueue.h"

// Records what cq_pump would have sent instead of building chunks
static int sent_fd[64];
static short sent_x[64], sent_z[64];
static int sent_count = 0;

static int record (int client_fd, short x, short z) {
  sent_fd[sent_count] = client_fd;
  sent_x[sent_count] = x;
  sent_z[sent_count] = z;
  sent_count ++;
  return 0;
}

static void fresh (void) {
  cq_send = record;
  sent_count = 0;
  for (int fd = 0; fd < 64; fd ++) cq_reset(fd);
}

static void test_push_does_not_duplicate (void) {
  fresh();
  CHECK(cq_push(5, 1, 2) == 0);
  CHECK(cq_push(5, 1, 2) == 1);
  CHECK(cq_push(5, 1, 3) == 0);
  CHECK(cq_pump(10) == 2);
  CHECK(sent_count == 2);
  CHECK(sent_x[0] == 1 && sent_z[0] == 2);   // in the order they were asked for
  CHECK(sent_x[1] == 1 && sent_z[1] == 3);
}

static void test_pump_respects_budget_and_alternates (void) {
  fresh();
  for (int i = 0; i < 4; i ++) {
    CHECK(cq_push(7, i, 0) == 0);
    CHECK(cq_push(9, i, 0) == 0);
  }
  CHECK(cq_pump(2) == 2);
  CHECK(sent_fd[0] != sent_fd[1]);           // one each, not two for one client
  CHECK(cq_pump(2) == 2);
  CHECK(sent_fd[2] != sent_fd[3]);
  CHECK(cq_pump(100) == 4);
  CHECK(cq_pump(100) == 0);
  CHECK(cq_pending(7) == 0 && cq_pending(9) == 0);
}

static void test_full_queue_refuses (void) {
  fresh();
  for (int i = 0; i < CHUNK_QUEUE_LEN; i ++) CHECK(cq_push(3, i, i) == 0);
  CHECK(cq_push(3, 999, 999) == -1);
  CHECK(cq_pending(3) == CHUNK_QUEUE_LEN);
  CHECK(cq_pump(1000) == CHUNK_QUEUE_LEN);
  for (int i = 0; i < sent_count; i ++) CHECK(sent_x[i] != 999);
}

static void test_reset_drops_a_client (void) {
  fresh();
  cq_push(4, 0, 0);
  cq_push(4, 0, 1);
  cq_reset(4);
  CHECK(cq_pending(4) == 0);
  CHECK(cq_pump(10) == 0);
}

static void test_more_clients_than_queues (void) {
  fresh();
  for (int fd = 0; fd < MAX_CONNECTIONS; fd ++) CHECK(cq_push(fd, 0, 0) == 0);
  CHECK(cq_push(100, 0, 0) == -1);         // no queue left for a ninth fd
}

int main (void) {
  RUN(test_push_does_not_duplicate);
  RUN(test_pump_respects_budget_and_alternates);
  RUN(test_full_queue_refuses);
  RUN(test_reset_drops_a_client);
  RUN(test_more_clients_than_queues);
  return CHECK_DONE();
}
