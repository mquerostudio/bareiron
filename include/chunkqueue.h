#ifndef H_CHUNKQUEUE
#define H_CHUNKQUEUE

// Chunks a client still has to receive. Building one chunk costs ~65 ms on
// the ESP32-P4, and a join needs 25, so they wait here and the main loop
// sends a few per iteration: other players keep getting their ticks while
// someone joins or runs across the world.

#define CHUNK_QUEUE_LEN 32       // a join (25) plus a crossing or two
#define CHUNKS_PER_ITERATION 1   // sent per main loop iteration, all clients together

void cq_reset (int client_fd);                  // forget a client's queue
void cq_clear (void);                           // forget every queue (server start)
int  cq_push (int client_fd, short x, short z); // 0 queued, 1 already queued, -1 no room
int  cq_pending (int client_fd);
int  cq_pump (int budget);                      // sends up to `budget` chunks, one client at a time; returns how many

// What cq_pump sends with (sc_chunkDataAndUpdateLight); tests swap it out
extern int (*cq_send)(int client_fd, short x, short z);

// Queues a chunk, or sends it right away if the queue is full
void queueChunk (int client_fd, short x, short z);

#endif
