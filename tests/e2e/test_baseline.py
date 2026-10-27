import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = 25565


class BaselineTest(unittest.TestCase):

  def test_baseline_join(self):
    with ServerProcess(PORT) as server:
      bot = Bot(port=PORT, name="baseline")
      bot.login()
      bot.play_for(5)
      print("\nbytes_received=%d chunks_received=%d" % (bot.bytes_received, bot.chunks_received))
      bot.close()
      # Uncompressed this was 3 630 016 bytes (docs/perf.md); compression
      # limits are checked in test_compression.py
      self.assertEqual(bot.chunks_received, 25)

  def test_status_ping_answers(self):
    with ServerProcess(PORT):
      info = Bot(port=PORT, name="pinger").status()
      self.assertEqual(info["version"]["protocol"], 772)
      self.assertEqual(info["description"]["text"], "KodeCraft")


if __name__ == "__main__":
  unittest.main()
