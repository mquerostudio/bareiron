#include <stdint.h>

#include "globals.h"
#include "packets.h"
#include "chunkqueue.h"

static int sendChunk (int client_fd, short x, short z) {
  return sc_chunkDataAndUpdateLight(client_fd, x, z);
}

int (*cq_send)(int client_fd, short x, short z) = sendChunk;

typedef struct {
  int fd;                        // -1 when the queue is free
  uint8_t head, count;
  short x[CHUNK_QUEUE_LEN];
  short z[CHUNK_QUEUE_LEN];
} ChunkQueue;

static ChunkQueue queues[MAX_CONNECTIONS];
static uint8_t queues_ready = false;
static int next_queue = 0;       // where the next pump starts, for fairness

static void cqInit (void) {
  if (queues_ready) return;
  for (int i = 0; i < MAX_CONNECTIONS; i ++) queues[i].fd = -1;
  queues_ready = true;
}

static ChunkQueue *findQueue (int client_fd) {
  cqInit();
  for (int i = 0; i < MAX_CONNECTIONS; i ++) {
    if (queues[i].fd == client_fd) return &queues[i];
  }
  return NULL;
}

void cq_clear (void) {
  queues_ready = false;
  cqInit();
  next_queue = 0;
}

void cq_reset (int client_fd) {
  ChunkQueue *queue = findQueue(client_fd);
  if (!queue) return;
  queue->fd = -1;
  queue->head = 0;
  queue->count = 0;
}

int cq_push (int client_fd, short x, short z) {
  ChunkQueue *queue = findQueue(client_fd);
  if (!queue) {
    queue = findQueue(-1);
    if (!queue) return -1;
    queue->fd = client_fd;
    queue->head = 0;
    queue->count = 0;
  }
  for (int i = 0; i < queue->count; i ++) {
    int slot = (queue->head + i) % CHUNK_QUEUE_LEN;
    if (queue->x[slot] == x && queue->z[slot] == z) return 1;
  }
  if (queue->count == CHUNK_QUEUE_LEN) return -1;
  int slot = (queue->head + queue->count) % CHUNK_QUEUE_LEN;
  queue->x[slot] = x;
  queue->z[slot] = z;
  queue->count ++;
  return 0;
}

int cq_pending (int client_fd) {
  ChunkQueue *queue = findQueue(client_fd);
  return queue ? queue->count : 0;
}

int cq_pump (int budget) {
  cqInit();
  int sent = 0;
  // One chunk per client per round, so nobody waits behind a whole join
  while (sent < budget) {
    int sent_this_round = 0;
    for (int n = 0; n < MAX_CONNECTIONS && sent < budget; n ++) {
      ChunkQueue *queue = &queues[(next_queue + n) % MAX_CONNECTIONS];
      if (queue->fd == -1 || queue->count == 0) continue;
      short x = queue->x[queue->head], z = queue->z[queue->head];
      queue->head = (queue->head + 1) % CHUNK_QUEUE_LEN;
      queue->count --;
      int fd = queue->fd;
      if (queue->count == 0) queue->fd = -1;   // free the queue for another client
      cq_send(fd, x, z);
      sent ++;
      sent_this_round ++;
    }
    next_queue = (next_queue + 1) % MAX_CONNECTIONS;
    if (sent_this_round == 0) break;
  }
  return sent;
}

void queueChunk (int client_fd, short x, short z) {
  if (cq_push(client_fd, x, z) < 0) cq_send(client_fd, x, z);
}
