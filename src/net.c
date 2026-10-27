#include <string.h>
#include <errno.h>

#ifdef ESP_PLATFORM
  #include "lwip/sockets.h"
#else
  #include <sys/socket.h>
#endif

#include "globals.h"
#include "net.h"

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
} NetClient;

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

// Moves every complete frame from the plain buffer to the wire buffer
static int frameOutgoing (NetClient *client) {
  size_t pos = 0;
  while (pos < client->plain.len) {
    uint32_t length;
    int size = peekVarInt(client->plain.data + pos, client->plain.len - pos, &length);
    if (size < 0) return -1;
    if (size == 0 || pos + size + length > client->plain.len) break;
    if (appendBuffer(&client->wire, client->plain.data + pos, size + length)) return -1;
    pos += size + length;
  }
  if (pos) consumeBuffer(&client->plain, pos);
  return 0;
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
      continue;
    }
    if (received == 0) return -1;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
  }
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
  if (appendBuffer(&client->packet, client->rx.data + size, frame_length)) return -1;
  consumeBuffer(&client->rx, size + frame_length);

  *length = frame_length;
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
