"""Minimal Minecraft Java 1.21.8 (protocol 772) client for end-to-end tests.

Speaks just enough of the protocol to log in, finish configuration, stay in
the Play state answering keep-alives, and move around. Understands protocol
compression once the server sends Set Compression.
"""

import json
import math
import socket
import struct
import time
import uuid
import zlib

PROTOCOL = 772

# Clientbound
LOGIN_SET_COMPRESSION = 0x03
LOGIN_SUCCESS = 0x02
CONFIG_FINISH = 0x03
CONFIG_KEEP_ALIVE = 0x04
PLAY_CHUNK = 0x27
PLAY_KEEP_ALIVE = 0x26
PLAY_SYNC_POSITION = 0x41
PLAY_DISCONNECT = 0x1C

# Serverbound
SB_CONFIG_CLIENT_INFO = 0x00
SB_CONFIG_FINISH_ACK = 0x03
SB_CONFIG_KEEP_ALIVE = 0x04
SB_CONFIG_KNOWN_PACKS = 0x07
SB_PLAY_CONFIRM_TELEPORT = 0x00
SB_PLAY_KEEP_ALIVE = 0x1B
SB_PLAY_SET_POSITION = 0x1D
SB_PLAY_PLAYER_ACTION = 0x28
SB_PLAY_PLAYER_LOADED = 0x2B


class Disconnected(Exception):
  pass


def varint(value):
  value &= 0xFFFFFFFF
  out = bytearray()
  while True:
    if value & ~0x7F == 0:
      out.append(value)
      return bytes(out)
    out.append((value & 0x7F) | 0x80)
    value >>= 7


def string(text):
  data = text.encode()
  return varint(len(data)) + data


def read_varint(buf, pos):
  value = shift = 0
  while True:
    if pos >= len(buf):
      raise IndexError("varint past end")
    byte = buf[pos]
    pos += 1
    value |= (byte & 0x7F) << shift
    if not byte & 0x80:
      break
    shift += 7
    if shift >= 35:
      raise ValueError("varint too long")
  if value & 0x80000000:
    value -= 1 << 32
  return value, pos


def block_position(x, y, z):
  return struct.pack(">q", ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF))


class Bot:
  def __init__(self, host="127.0.0.1", port=25565, name="bot"):
    self.host, self.port, self.name = host, port, name
    self.sock = None
    self.rx = bytearray()
    self.threshold = -1
    self.bytes_received = 0
    self.chunks_received = 0
    self.keep_alives = []        # monotonic timestamps of received keep-alives
    self.position = None         # last (x, y, z) sent by the server
    self.state = "none"

  # -- transport --------------------------------------------------------

  def _connect(self):
    self.sock = socket.create_connection((self.host, self.port), timeout=10)
    self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

  def send(self, packet_id, payload=b""):
    data = varint(packet_id) + payload
    if self.threshold >= 0:
      if len(data) >= self.threshold:
        body = varint(len(data)) + zlib.compress(data)
      else:
        body = varint(0) + data
    else:
      body = data
    self.sock.sendall(varint(len(body)) + body)

  def _fill(self, timeout):
    self.sock.settimeout(timeout)
    try:
      chunk = self.sock.recv(1 << 16)
    except socket.timeout:
      return False
    if not chunk:
      raise Disconnected(self.name + ": server closed the connection")
    self.bytes_received += len(chunk)
    self.rx += chunk
    return True

  def recv(self, timeout=10.0):
    """Return (packet_id, payload) of the next packet, or None on timeout."""
    deadline = time.monotonic() + timeout
    while True:
      try:
        length, pos = read_varint(self.rx, 0)
        if len(self.rx) >= pos + length:
          frame = bytes(self.rx[pos:pos + length])
          del self.rx[:pos + length]
          return self._unframe(frame)
      except IndexError:
        pass
      left = deadline - time.monotonic()
      if left <= 0 or not self._fill(left):
        return None

  def _unframe(self, frame):
    if self.threshold >= 0:
      data_length, pos = read_varint(frame, 0)
      frame = zlib.decompress(frame[pos:]) if data_length else frame[pos:]
    packet_id, pos = read_varint(frame, 0)
    return packet_id, frame[pos:]

  def close(self, abrupt=False):
    if not self.sock:
      return
    if abrupt:
      self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    self.sock.close()
    self.sock = None

  # -- protocol flows ----------------------------------------------------

  def _handshake(self, intent):
    self.send(0x00, varint(PROTOCOL) + string(self.host) + struct.pack(">H", self.port) + varint(intent))

  def status(self):
    self._connect()
    self._handshake(1)
    self.send(0x00)
    packet = self.recv()
    if packet is None:
      raise TimeoutError("no status response")
    length, pos = read_varint(packet[1], 0)
    info = json.loads(packet[1][pos:pos + length])
    self.send(0x01, struct.pack(">q", 42))
    self.recv(timeout=5)
    self.close()
    return info

  def login(self, timeout=20.0):
    """Log in and return once the server has placed the player in the world."""
    self._connect()
    self._handshake(2)
    player_uuid = uuid.uuid3(uuid.NAMESPACE_OID, "OfflinePlayer:" + self.name).bytes
    self.send(0x00, string(self.name) + player_uuid)
    self.state = "login"
    deadline = time.monotonic() + timeout
    while self.position is None:
      packet = self.recv(timeout=max(0.1, deadline - time.monotonic()))
      if packet is None:
        raise TimeoutError(self.name + ": login timed out in state " + self.state)
      self._handle(*packet)

  def _handle(self, packet_id, payload):
    if self.state == "login":
      if packet_id == LOGIN_SET_COMPRESSION:
        self.threshold, _ = read_varint(payload, 0)
      elif packet_id == LOGIN_SUCCESS:
        self.send(0x03)  # Login Acknowledged
        self.state = "config"
        self.send(SB_CONFIG_CLIENT_INFO, string("en_us") + bytes([2]) + varint(0) + bytes([1, 0x7F])
                  + varint(1) + bytes([0, 1]) + varint(0))
    elif self.state == "config":
      if packet_id == 0x0E:  # Clientbound Known Packs
        self.send(SB_CONFIG_KNOWN_PACKS, varint(0))
      elif packet_id == CONFIG_KEEP_ALIVE:
        self.send(SB_CONFIG_KEEP_ALIVE, payload)
      elif packet_id == CONFIG_FINISH:
        self.send(SB_CONFIG_FINISH_ACK)
        self.state = "play"
    elif self.state == "play":
      if packet_id == PLAY_KEEP_ALIVE:
        self.keep_alives.append(time.monotonic())
        self.send(SB_PLAY_KEEP_ALIVE, payload)
      elif packet_id == PLAY_CHUNK:
        self.chunks_received += 1
      elif packet_id == PLAY_SYNC_POSITION:
        teleport_id, pos = read_varint(payload, 0)
        x, y, z = struct.unpack(">ddd", payload[pos:pos + 24])
        self.position = (x, y, z)
        self.send(SB_PLAY_CONFIRM_TELEPORT, varint(teleport_id))
        self.send(SB_PLAY_PLAYER_LOADED)
      elif packet_id == PLAY_DISCONNECT:
        raise Disconnected(self.name + ": kicked")

  def pump(self, seconds):
    """Process incoming packets for `seconds`."""
    end = time.monotonic() + seconds
    while True:
      left = end - time.monotonic()
      if left <= 0:
        return
      packet = self.recv(timeout=left)
      if packet is not None:
        self._handle(*packet)

  def play_for(self, seconds):
    self.pump(seconds)

  def move_to(self, x, y, z):
    self.send(SB_PLAY_SET_POSITION, struct.pack(">dddB", x, y, z, 1))
    self.position = (x, y, z)

  def walk_circle(self, seconds, radius=40.0, step_s=0.05):
    """Walk in a circle around the spawn point, answering keep-alives."""
    cx, y, cz = self.position
    start = time.monotonic()
    while time.monotonic() - start < seconds:
      angle = (time.monotonic() - start) * 0.5
      self.move_to(cx + radius * math.cos(angle), y, cz + radius * math.sin(angle))
      self.pump(step_s)

  def break_block(self, x, y, z):
    for status in (0, 2):
      self.send(SB_PLAY_PLAYER_ACTION, varint(status) + block_position(x, y, z) + bytes([1]) + varint(1))
