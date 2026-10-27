#include <string.h>
#include <errno.h>

#ifdef ESP_PLATFORM
  #include "lwip/sockets.h"
#else
  #include <sys/socket.h>
#endif

#include "globals.h"
#include "tools.h"
#include "net.h"
#include "miniz.h"

#ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
#endif

// A growable byte buffer living in large (PSRAM) memory
typedef struct {
  uint8_t *data;
  size_t len;
  size_t cap;
} NetBuffer;

typedef struct {
  int fd;
  // Bytes written by the packet handlers, not yet split into frames
  NetBuffer plain;
  // Whole frames waiting for the socket, sent from `wire_off` onwards
  NetBuffer wire;
  size_t wire_off;
  // Raw bytes read from the socket, not yet split into packets
  NetBuffer rx;
  // The packet being handled, read from `packet_pos` onwards
  NetBuffer packet;
  size_t packet_pos;
  // Packets of at least this many bytes get compressed, -1 = compression off
  int threshold;
  // When the client last sent anything
  int64_t last_rx_us;
} NetClient;

// Deflate and inflate state is big (~300 KB), so there's one of each,
// shared by all clients and allocated on first use
static tdefl_compressor *deflater = NULL;
static tinfl_decompressor *inflater = NULL;
static NetBuffer scratch = { NULL, 0, 0 };

static NetClient net_clients[MAX_CONNECTIONS] = { [0 ... MAX_CONNECTIONS - 1] = { .fd = -1 } };

static NetClient *findClient (int fd) {
  if (fd < 0) return NULL;
  for (int i = 0; i < MAX_CONNECTIONS; i ++) {
    if (net_clients[i].fd == fd) return &net_clients[i];
  }
  return NULL;
}

static void freeBuffer (NetBuffer *buffer) {
  if (buffer->data) platform->free_large(buffer->data);
  buffer->data = NULL;
  buffer->len = buffer->cap = 0;
}

// Makes room for `extra` more bytes, returns 0 on success
static int reserveBuffer (NetBuffer *buffer, size_t extra) {
  if (buffer->len + extra <= buffer->cap) return 0;
  size_t cap = buffer->cap ? buffer->cap : 4096;
  while (cap < buffer->len + extra) cap *= 2;
  uint8_t *data = platform->alloc_large(cap);
  if (!data) return -1;
  if (buffer->len) memcpy(data, buffer->data, buffer->len);
  if (buffer->data) platform->free_large(buffer->data);
  buffer->data = data;
  buffer->cap = cap;
  return 0;
}

static int appendBuffer (NetBuffer *buffer, const void *src, size_t len) {
  if (reserveBuffer(buffer, len)) return -1;
  memcpy(buffer->data + buffer->len, src, len);
  buffer->len += len;
  return 0;
}

// Drops the first `count` bytes of a buffer
static void consumeBuffer (NetBuffer *buffer, size_t count) {
  memmove(buffer->data, buffer->data + count, buffer->len - count);
  buffer->len -= count;
}

// Parses a VarInt at the start of `data`. Returns its size in bytes,
// 0 if more bytes are needed, or -1 if it's malformed.
static int peekVarInt (const uint8_t *data, size_t len, uint32_t *value) {
  *value = 0;
  for (int i = 0; i < 5; i ++) {
    if ((size_t)i >= len) return 0;
    *value |= (uint32_t)(data[i] & 0x7F) << (7 * i);
    if (!(data[i] & 0x80)) return i + 1;
  }
  return -1;
}

int net_open (int fd) {
  NetClient *client = NULL;
  for (int i = 0; i < MAX_CONNECTIONS && !client; i ++) {
    if (net_clients[i].fd == -1) client = &net_clients[i];
  }
  if (!client) return -1;
  memset(client, 0, sizeof(*client));
  client->fd = fd;
  client->threshold = -1;
  client->last_rx_us = get_program_time();
  return 0;
}

void net_close (int fd) {
  NetClient *client = findClient(fd);
  if (!client) return;
  freeBuffer(&client->plain);
  freeBuffer(&client->wire);
  freeBuffer(&client->rx);
  freeBuffer(&client->packet);
  client->fd = -1;
}

ssize_t net_write (int fd, const void *buf, size_t len) {
  NetClient *client = findClient(fd);
  if (!client) return -1;
  if (appendBuffer(&client->plain, buf, len)) return -1;
  return len;
}

static int appendVarInt (NetBuffer *buffer, uint32_t value) {
  uint8_t bytes[5];
  int size = 0;
  while (value & ~0x7Fu) {
    bytes[size ++] = (value & 0x7F) | 0x80;
    value >>= 7;
  }
  bytes[size ++] = value;
  return appendBuffer(buffer, bytes, size);
}

static int varIntSize (uint32_t value) {
  int size = 1;
  while (value & ~0x7Fu) { value >>= 7; size ++; }
  return size;
}

static mz_bool appendDeflated (const void *data, int len, void *user) {
  return appendBuffer((NetBuffer *)user, data, len) == 0;
}

// Appends one packet (ID + data) to the wire buffer in the compressed
// frame format: [packet length][data length][zlib data], where a data
// length of 0 means the packet is below the threshold and sent as is
static int appendCompressedFrame (NetClient *client, const uint8_t *packet, uint32_t len) {
  if ((int)len < client->threshold) {
    if (appendVarInt(&client->wire, len + 1)) return -1;
    if (appendVarInt(&client->wire, 0)) return -1;
    return appendBuffer(&client->wire, packet, len);
  }

  if (!deflater) {
    deflater = platform->alloc_large(sizeof(tdefl_compressor));
    if (!deflater) return -1;
  }
  scratch.len = 0;
  // Level 1: favour speed, chunks are mostly air and stone anyway
  tdefl_init(deflater, appendDeflated, &scratch, tdefl_create_comp_flags_from_zip_params(1, 15, 0));
  if (tdefl_compress_buffer(deflater, packet, len, TDEFL_FINISH) != TDEFL_STATUS_DONE) return -1;

  if (appendVarInt(&client->wire, varIntSize(len) + scratch.len)) return -1;
  if (appendVarInt(&client->wire, len)) return -1;
  return appendBuffer(&client->wire, scratch.data, scratch.len);
}

// Moves every complete frame from the plain buffer to the wire buffer
static int frameOutgoing (NetClient *client) {
  size_t pos = 0;
  while (pos < client->plain.len) {
    uint32_t length;
    int size = peekVarInt(client->plain.data + pos, client->plain.len - pos, &length);
    if (size < 0) return -1;
    if (size == 0 || pos + size + length > client->plain.len) break;
    int failed;
    if (client->threshold < 0) {
      failed = appendBuffer(&client->wire, client->plain.data + pos, size + length);
    } else {
      failed = appendCompressedFrame(client, client->plain.data + pos + size, length);
    }
    if (failed) return -1;
    pos += size + length;
  }
  if (pos) consumeBuffer(&client->plain, pos);
  return 0;
}

void net_enable_compression (int fd, int threshold) {
  NetClient *client = findClient(fd);
  if (!client) return;
  // Whatever was written so far goes out in the old format
  frameOutgoing(client);
  client->threshold = threshold;
}

int net_flush (int fd) {
  NetClient *client = findClient(fd);
  if (!client) return 0;
  if (frameOutgoing(client)) return -2;

  // Send as much as the socket takes right now, keep the rest for later
  while (client->wire_off < client->wire.len) {
    ssize_t sent = send(fd, client->wire.data + client->wire_off,
                        client->wire.len - client->wire_off, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent > 0) {
      client->wire_off += sent;
      continue;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
    return -1;
  }

  // Compact the wire buffer once everything before wire_off is gone
  if (client->wire_off == client->wire.len) {
    client->wire.len = client->wire_off = 0;
  } else if (client->wire_off > client->wire.cap / 2) {
    consumeBuffer(&client->wire, client->wire_off);
    client->wire_off = 0;
  }

  if (client->wire.len - client->wire_off + client->plain.len > NET_TX_CAP) return -2;
  return 0;
}

size_t net_pending (int fd) {
  NetClient *client = findClient(fd);
  if (!client) return 0;
  return client->wire.len - client->wire_off;
}

int net_pump (int fd) {
  NetClient *client = findClient(fd);
  if (!client) return -1;
  while (true) {
    // Don't let a client flood memory: stop reading once a full packet
    // of the largest size is already waiting
    if (client->rx.len > NET_MAX_PACKET + 8) return 0;
    if (reserveBuffer(&client->rx, 4096)) return -1;
    ssize_t received = recv(fd, client->rx.data + client->rx.len, client->rx.cap - client->rx.len, MSG_DONTWAIT);
    if (received > 0) {
      client->rx.len += received;
      client->last_rx_us = get_program_time();
      continue;
    }
    if (received == 0) return -1;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
  }
}

int64_t net_idle_us (int fd) {
  NetClient *client = findClient(fd);
  if (!client) return 0;
  return get_program_time() - client->last_rx_us;
}

int net_next_packet (int fd, int *length) {
  NetClient *client = findClient(fd);
  if (!client) return -1;

  uint32_t frame_length;
  int size = peekVarInt(client->rx.data, client->rx.len, &frame_length);
  if (size < 0 || frame_length > NET_MAX_PACKET) return -1;
  if (size == 0 || client->rx.len < size + frame_length) return 0;

  client->packet.len = 0;
  client->packet_pos = 0;
  const uint8_t *frame = client->rx.data + size;

  if (client->threshold < 0) {
    if (appendBuffer(&client->packet, frame, frame_length)) return -1;
  } else {
    // [data length][data], where data is zlib-compressed unless its length is 0
    uint32_t data_length;
    int data_size = peekVarInt(frame, frame_length, &data_length);
    if (data_size <= 0 || data_length > NET_MAX_PACKET) return -1;
    if (data_length == 0) {
      if (appendBuffer(&client->packet, frame + data_size, frame_length - data_size)) return -1;
    } else {
      if (!inflater) {
        inflater = platform->alloc_large(sizeof(tinfl_decompressor));
        if (!inflater) return -1;
      }
      if (reserveBuffer(&client->packet, data_length)) return -1;
      tinfl_init(inflater);
      size_t in_len = frame_length - data_size, out_len = data_length;
      tinfl_status status = tinfl_decompress(inflater, frame + data_size, &in_len,
        client->packet.data, client->packet.data, &out_len,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
      if (status != TINFL_STATUS_DONE || out_len != data_length) return -1;
      client->packet.len = data_length;
    }
  }
  consumeBuffer(&client->rx, size + frame_length);

  *length = client->packet.len;
  return 1;
}

ssize_t net_read (int fd, void *buf, size_t n) {
  NetClient *client = findClient(fd);
  if (!client) return -1;
  size_t left = client->packet.len - client->packet_pos;
  if (n > left) n = left;
  if (n == 0) return 0;
  memcpy(buf, client->packet.data + client->packet_pos, n);
  client->packet_pos += n;
  return n;
}

void net_finish_packet (int fd) {
  NetClient *client = findClient(fd);
  if (!client) return;
  client->packet.len = 0;
  client->packet_pos = 0;
}
