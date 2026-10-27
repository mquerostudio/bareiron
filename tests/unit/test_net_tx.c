// Outgoing network layer: buffered writes, non-blocking flush, size cap
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "check.h"
#include "globals.h"
#include "net.h"

static int64_t now_us (void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// Returns the server end in `server`, the peer end in `peer`
static void makePair (int *server, int *peer) {
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
  fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
  *server = fds[0];
  *peer = fds[1];
}

static void test_write_is_buffered_until_flush (void) {
  int server, peer;
  makePair(&server, &peer);
  CHECK(net_open(server) == 0);

  // A whole frame: length 2, then two bytes of packet
  uint8_t frame[3] = { 0x02, 0x26, 0x07 };
  CHECK(net_write(server, frame, 3) == 3);

  uint8_t got[8];
  CHECK(recv(peer, got, sizeof(got), 0) == -1); // nothing sent yet

  CHECK(net_flush(server) == 0);
  CHECK(recv(peer, got, sizeof(got), 0) == 3);
  CHECK(memcmp(got, frame, 3) == 0);

  net_close(server);
  close(server);
  close(peer);
}

static void test_unknown_fd_is_ignored (void) {
  uint8_t byte = 0;
  CHECK(net_write(-1, &byte, 1) == -1);
  CHECK(net_flush(-1) == 0);
}

static void test_peer_not_reading_hits_cap (void) {
  int server, peer;
  makePair(&server, &peer);
  CHECK(net_open(server) == 0);

  // 64 KiB frames: length prefix 0x80 0x80 0x04 (65536), then the body
  static uint8_t frame[3 + 65536];
  frame[0] = 0x80; frame[1] = 0x80; frame[2] = 0x04;

  int result = 0;
  int64_t worst = 0;
  for (int i = 0; i < 200 && result == 0; i ++) {
    net_write(server, frame, sizeof(frame));
    int64_t start = now_us();
    result = net_flush(server);
    int64_t took = now_us() - start;
    if (took > worst) worst = took;
  }
  CHECK(result == -2);
  CHECK(worst < 50000);

  net_close(server);
  close(server);
  close(peer);
}

// A join writes ~3.6 MB of chunk packets in one go. Frames must be
// compressed as they're written, not held raw until the next flush, and
// the buffers must shrink back once the burst has gone out.
static void test_join_burst_stays_small (void) {
  int server, peer;
  makePair(&server, &peer);
  CHECK(net_open(server) == 0);
  net_enable_compression(server, 256);

  static uint8_t frame[3 + 140000];
  frame[0] = 0xE0; frame[1] = 0xA5; frame[2] = 0x08; // 140000
  for (size_t i = 3; i < sizeof(frame); i ++) frame[i] = (i % 4096) < 2048 ? 1 : (uint8_t)(i / 4096);
  for (int i = 0; i < 25; i ++) CHECK(net_write(server, frame, sizeof(frame)) == (ssize_t)sizeof(frame));
  printf("  buffers after burst: %zu bytes\n", net_memory(server));
  CHECK(net_memory(server) < 1024 * 1024);

  // Drain everything, then the buffers give their memory back
  uint8_t sink[65536];
  for (int i = 0; i < 100000 && (net_pending(server) || net_memory(server) > 65536); i ++) {
    CHECK(net_flush(server) == 0);
    while (recv(peer, sink, sizeof(sink), 0) > 0);
  }
  printf("  buffers after drain: %zu bytes\n", net_memory(server));
  CHECK(net_memory(server) <= 65536);

  net_close(server);
  close(server);
  close(peer);
}

// When memory runs out mid-packet the stream is broken: the client must
// be dropped, never sent half a packet
static int fail_allocations = 0;
static void *failingAlloc (size_t size) { return fail_allocations ? NULL : malloc(size); }

static void test_failed_allocation_drops_client (void) {
  BareironPlatform failing = *bareiron_posix_platform();
  failing.alloc_large = failingAlloc;
  platform = &failing;

  int server, peer;
  makePair(&server, &peer);
  CHECK(net_open(server) == 0);
  uint8_t small[3] = { 0x02, 0x26, 0x01 };
  CHECK(net_write(server, small, 3) == 3);
  fail_allocations = 1;
  static uint8_t big[8192];
  CHECK(net_write(server, big, sizeof(big)) == -1);
  CHECK(net_flush(server) == -1);
  fail_allocations = 0;

  net_close(server);
  close(server);
  close(peer);
  platform = bareiron_posix_platform();
}

int main (void) {
  platform = bareiron_posix_platform();
  RUN(test_write_is_buffered_until_flush);
  RUN(test_unknown_fd_is_ignored);
  RUN(test_peer_not_reading_hits_cap);
  RUN(test_join_burst_stays_small);
  RUN(test_failed_allocation_drops_client);
  return CHECK_DONE();
}
