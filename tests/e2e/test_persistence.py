import os
import re
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken


class PersistenceTest(unittest.TestCase):

  def test_stop_and_restart_keeps_world(self):
    world = tempfile.mkdtemp(prefix="bareiron-persist-")

    first = ServerProcess(PORT, world).start()
    bot = Bot(port=PORT, name="miner")
    bot.login()
    bot.play_for(3)
    x, y, z = (int(v // 1) for v in bot.position)
    bot.break_block(x, y - 1, z)
    # The status snapshot refreshes once a second
    deadline = time.monotonic() + 3
    while first.status()["block_changes_used"] < 1 and time.monotonic() < deadline:
      bot.play_for(0.2)
    self.assertGreaterEqual(first.status()["block_changes_used"], 1)
    code = first.stop()
    bot.close()
    self.assertEqual(code, 0, first.log[-1500:])
    self.assertTrue(any(e["type"] == "world_saved" for e in first.events()), first.log[-1500:])
    seed = re.search(r"seed ([0-9A-F]{8})", first.log).group(1)

    second = ServerProcess(PORT, world).start()
    try:
      time.sleep(1.2)
      loaded = re.search(r"World loaded: (\d+) block changes, seed ([0-9A-F]{8})", second.log)
      self.assertIsNotNone(loaded, second.log[-1500:])
      self.assertGreaterEqual(int(loaded.group(1)), 1)
      self.assertEqual(loaded.group(2), seed)
      self.assertGreaterEqual(second.status()["block_changes_used"], 1)
    finally:
      second.stop()


if __name__ == "__main__":
  unittest.main()
