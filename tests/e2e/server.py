"""Runs the bareiron binary for end-to-end tests and captures its output."""

import json
import os
import signal
import socket
import subprocess
import tempfile
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BINARY = os.path.join(ROOT, "bareiron")


class ServerProcess:
  def __init__(self, port=25565, world_dir=None):
    self.port = port
    self.world_dir = world_dir or tempfile.mkdtemp(prefix="bareiron-world-")
    self.proc = None
    self.lines = []
    self._lock = threading.Lock()

  def start(self, timeout=10.0):
    env = dict(os.environ, BAREIRON_PORT=str(self.port))
    self.proc = subprocess.Popen(
      [BINARY], cwd=self.world_dir, env=env,
      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
    )
    threading.Thread(target=self._reader, daemon=True).start()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
      try:
        socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
        return self
      except OSError:
        if self.proc.poll() is not None:
          raise RuntimeError("server exited early:\n" + self.log)
        time.sleep(0.05)
    raise TimeoutError("server did not start listening:\n" + self.log)

  def _reader(self):
    for line in self.proc.stdout:
      with self._lock:
        self.lines.append(line.rstrip("\n"))

  @property
  def log(self):
    with self._lock:
      return "\n".join(self.lines)

  def events(self):
    with self._lock:
      return [json.loads(l[6:]) for l in self.lines if l.startswith("EVENT ")]

  def status(self, timeout=2.0):
    with self._lock:
      seen = len(self.lines)
    self.proc.send_signal(signal.SIGUSR1)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
      with self._lock:
        fresh = [l for l in self.lines[seen:] if l.startswith("STATUS ")]
      if fresh:
        return json.loads(fresh[-1][7:])
      time.sleep(0.02)
    raise TimeoutError("no STATUS line")

  def stop(self, timeout=10.0):
    if self.proc and self.proc.poll() is None:
      self.proc.send_signal(signal.SIGINT)
      try:
        self.proc.wait(timeout)
      except subprocess.TimeoutExpired:
        self.proc.kill()
        self.proc.wait()
    if self.proc and self.proc.stdout:
      self.proc.stdout.close()
    return self.proc.returncode

  def __enter__(self):
    return self.start()

  def __exit__(self, *exc):
    self.stop()
