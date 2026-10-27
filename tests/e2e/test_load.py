import os
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = 25565


def keep_alive_gap(bot, until):
  """Longest time without a keep-alive between the first one and `until`."""
  stamps = bot.keep_alives + [until]
  return max(b - a for a, b in zip(stamps, stamps[1:])) if len(stamps) > 1 else float("inf")


class LoadTest(unittest.TestCase):

  def test_six_bots_one_minute(self):
    with ServerProcess(PORT) as server:
      bots = [Bot(port=PORT, name="walker%d" % i) for i in range(6)]
      errors = []

      def run(bot, delay):
        try:
          time.sleep(delay)
          bot.login()
          bot.walk_circle(60)
        except Exception as error:  # report every bot's failure, not just the first
          errors.append("%s: %r" % (bot.name, error))

      threads = [threading.Thread(target=run, args=(bot, i)) for i, bot in enumerate(bots)]
      for thread in threads:
        thread.start()

      worst_loop = 0
      while any(thread.is_alive() for thread in threads):
        time.sleep(2)
        worst_loop = max(worst_loop, server.status()["loop_max_us"])
      end = time.monotonic()

      print("\nworst loop iteration: %d us, players seen: %d" % (worst_loop, len(bots)))
      self.assertEqual(errors, [])
      for bot in bots:
        self.assertLess(keep_alive_gap(bot, end), 20, bot.name)
        bot.close()
      self.assertLess(worst_loop, 250_000)


if __name__ == "__main__":
  unittest.main()
