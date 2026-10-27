import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = 25565
BASELINE_JOIN_BYTES = 3_630_016  # docs/perf.md, upstream 02733be


class CompressionTest(unittest.TestCase):

  def test_join_bytes_with_compression(self):
    with ServerProcess(PORT):
      bot = Bot(port=PORT, name="compressed")
      bot.login()
      bot.play_for(5)
      bot.close()
      print("\nbytes_received=%d chunks_received=%d threshold=%d" % (bot.bytes_received, bot.chunks_received, bot.threshold))
      self.assertEqual(bot.threshold, 256)
      self.assertEqual(bot.chunks_received, 25)
      self.assertLess(bot.bytes_received, 0.25 * BASELINE_JOIN_BYTES)


if __name__ == "__main__":
  unittest.main()
