// Protocol compression: frame format in both directions
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "check.h"
#include "globals.h"
#include "net.h"
#include "miniz.h"

static void makePair (int *server, int *peer) {
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
  fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
  *server = fds[0];
  *peer = fds[1];
  CHECK(net_open(*server) == 0);
}

static void closePair (int server, int peer) {
  net_close(server);
  close(server);
  close(peer);
}

static size_t putVarInt (uint8_t *out, uint32_t value) {
  size_t n = 0;
  while (value & ~0x7Fu) { out[n ++] = (value & 0x7F) | 0x80; value >>= 7; }
  out[n ++] = value;
  return n;
}

static size_t getVarInt (const uint8_t *in, uint32_t *value) {
  size_t n = 0;
  *value = 0;
  do { *value |= (uint32_t)(in[n] & 0x7F) << (7 * n); } while (in[n ++] & 0x80);
  return n;
}

// Flushes the server end and drains the peer until `want` bytes arrived
static size_t drain (int server, int peer, uint8_t *out, size_t want) {
  size_t got = 0;
  for (int tries = 0; tries < 100000 && got < want; tries ++) {
    net_flush(server);
    ssize_t n = recv(peer, out + got, want - got, 0);
    if (n > 0) got += n;
  }
  return got;
}

// Writes one plain frame (length prefix + body) into the server end
static void writeFrame (int server, const uint8_t *body, size_t len) {
  uint8_t prefix[5];
  size_t n = putVarInt(prefix, len);
  net_write(server, prefix, n);
  net_write(server, body, len);
}

static void test_small_packet_uncompressed_format (void) {
  int server, peer;
  makePair(&server, &peer);
  net_enable_compression(server, 256);
  uint8_t body[10] = { 0x26, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  writeFrame(server, body, 10);
  uint8_t got[12];
  CHECK(drain(server, peer, got, 12) == 12);
  CHECK(got[0] == 11);
  CHECK(got[1] == 0x00);
  CHECK(memcmp(got + 2, body, 10) == 0);
  closePair(server, peer);
}

static void test_large_packet_roundtrip (void) {
  int server, peer;
  makePair(&server, &peer);
  net_enable_compression(server, 256);

  static uint8_t body[150000], got[200000], inflated[150000];
  for (size_t i = 0; i < sizeof(body); i ++) body[i] = (i % 7) * (i / 4096);
  writeFrame(server, body, sizeof(body));

  // Read the packet length first, then the rest of the frame
  size_t got_len = drain(server, peer, got, 3);
  uint32_t packet_length;
  size_t prefix = getVarInt(got, &packet_length);
  got_len += drain(server, peer, got + got_len, prefix + packet_length - got_len);
  CHECK(got_len == prefix + packet_length);

  uint32_t data_length;
  size_t data_prefix = getVarInt(got + prefix, &data_length);
  CHECK(data_length == sizeof(body));
  CHECK(packet_length - data_prefix < sizeof(body) / 2);
  size_t out = tinfl_decompress_mem_to_mem(inflated, sizeof(inflated),
    got + prefix + data_prefix, packet_length - data_prefix, TINFL_FLAG_PARSE_ZLIB_HEADER);
  CHECK(out == sizeof(body));
  CHECK(memcmp(inflated, body, sizeof(body)) == 0);
  closePair(server, peer);
}

static void test_frames_before_enable_stay_plain (void) {
  int server, peer;
  makePair(&server, &peer);
  uint8_t a[3] = { 0x03, 0xAA, 0xBB }, b[2] = { 0x01, 0xCC };
  writeFrame(server, a, 3);
  net_enable_compression(server, 256);
  writeFrame(server, b, 2);
  uint8_t got[8];
  CHECK(drain(server, peer, got, 8) == 8);
  // A: [3][03 AA BB], B: [3][00][01 CC]
  uint8_t want[8] = { 3, 0x03, 0xAA, 0xBB, 3, 0x00, 0x01, 0xCC };
  CHECK(memcmp(got, want, 8) == 0);
  closePair(server, peer);
}

static void test_rx_compressed_frame (void) {
  int server, peer, length;
  makePair(&server, &peer);
  net_enable_compression(server, 256);

  static uint8_t data[300], deflated[400], frame[420];
  for (int i = 0; i < 300; i ++) data[i] = i % 13;
  size_t deflated_len = tdefl_compress_mem_to_mem(deflated, sizeof(deflated), data, 300,
    tdefl_create_comp_flags_from_zip_params(1, 15, 0));
  CHECK(deflated_len > 0);

  uint8_t data_length[5];
  size_t dl = putVarInt(data_length, 300);
  size_t n = putVarInt(frame, dl + deflated_len);
  memcpy(frame + n, data_length, dl);
  memcpy(frame + n + dl, deflated, deflated_len);
  CHECK(write(peer, frame, n + dl + deflated_len) == (ssize_t)(n + dl + deflated_len));

  // A small one right after, in the uncompressed format
  uint8_t small[4] = { 3, 0x00, 0x1B, 0x05 };
  CHECK(write(peer, small, 4) == 4);

  CHECK(net_pump(server) == 0);
  CHECK(net_next_packet(server, &length) == 1);
  CHECK(length == 300);
  uint8_t got[300];
  CHECK(net_read(server, got, 300) == 300);
  CHECK(memcmp(got, data, 300) == 0);
  net_finish_packet(server);

  CHECK(net_next_packet(server, &length) == 1);
  CHECK(length == 2);
  CHECK(net_read(server, got, 2) == 2 && got[0] == 0x1B && got[1] == 0x05);
  net_finish_packet(server);
  closePair(server, peer);
}

int main (void) {
  platform = bareiron_posix_platform();
  RUN(test_small_packet_uncompressed_format);
  RUN(test_large_packet_roundtrip);
  RUN(test_frames_before_enable_stay_plain);
  RUN(test_rx_compressed_frame);
  return CHECK_DONE();
}
