// Incoming network layer: packets are handled only once complete
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "check.h"
#include "globals.h"
#include "net.h"

static void makePair (int *server, int *peer) {
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
  *server = fds[0];
  *peer = fds[1];
  CHECK(net_open(*server) == 0);
}

static void closePair (int server, int peer) {
  net_close(server);
  close(server);
  close(peer);
}

static void test_partial_frame_waits (void) {
  int server, peer, length = -1;
  makePair(&server, &peer);
  uint8_t frame[5] = { 0x04, 0x1B, 0x01, 0x02, 0x03 };

  CHECK(write(peer, frame, 2) == 2);
  CHECK(net_pump(server) == 0);
  CHECK(net_next_packet(server, &length) == 0);

  CHECK(write(peer, frame + 2, 3) == 3);
  CHECK(net_pump(server) == 0);
  CHECK(net_next_packet(server, &length) == 1);
  CHECK(length == 4);
  uint8_t got[4];
  CHECK(net_read(server, got, 4) == 4);
  CHECK(memcmp(got, frame + 1, 4) == 0);
  net_finish_packet(server);

  closePair(server, peer);
}

static void test_two_frames_in_one_read (void) {
  int server, peer, length = -1;
  makePair(&server, &peer);
  uint8_t frames[6] = { 0x02, 0x0C, 0xAA, 0x03, 0x1B, 0xBB, };
  // Second frame is cut short on purpose: its length says 3, only 2 sent
  CHECK(write(peer, frames, 6) == 6);
  CHECK(net_pump(server) == 0);

  CHECK(net_next_packet(server, &length) == 1);
  CHECK(length == 2);
  uint8_t got[3];
  CHECK(net_read(server, got, 2) == 2 && got[0] == 0x0C && got[1] == 0xAA);
  net_finish_packet(server);

  CHECK(net_next_packet(server, &length) == 0);
  uint8_t last = 0xCC;
  CHECK(write(peer, &last, 1) == 1);
  CHECK(net_pump(server) == 0);
  CHECK(net_next_packet(server, &length) == 1);
  CHECK(length == 3);
  CHECK(net_read(server, got, 3) == 3 && got[0] == 0x1B && got[2] == 0xCC);
  net_finish_packet(server);

  closePair(server, peer);
}

static void test_read_past_end_is_short (void) {
  int server, peer, length;
  makePair(&server, &peer);
  uint8_t frame[3] = { 0x02, 0x0C, 0x01 };
  CHECK(write(peer, frame, 3) == 3);
  CHECK(net_pump(server) == 0);
  CHECK(net_next_packet(server, &length) == 1);
  uint8_t got[8];
  CHECK(net_read(server, got, 8) == 2);
  CHECK(net_read(server, got, 1) == 0);
  net_finish_packet(server);
  closePair(server, peer);
}

static void test_oversized_frame_rejected (void) {
  int server, peer, length;
  makePair(&server, &peer);
  // Length prefix announcing 4 MiB
  uint8_t prefix[4] = { 0x80, 0x80, 0x80, 0x02 };
  CHECK(write(peer, prefix, 4) == 4);
  CHECK(net_pump(server) == 0);
  CHECK(net_next_packet(server, &length) == -1);
  closePair(server, peer);
}

static void test_peer_closed_reports_gone (void) {
  int server, peer;
  makePair(&server, &peer);
  close(peer);
  CHECK(net_pump(server) == -1);
  net_close(server);
  close(server);
}

int main (void) {
  platform = bareiron_posix_platform();
  RUN(test_partial_frame_waits);
  RUN(test_two_frames_in_one_read);
  RUN(test_read_past_end_is_short);
  RUN(test_oversized_frame_rejected);
  RUN(test_peer_closed_reports_gone);
  return CHECK_DONE();
}
