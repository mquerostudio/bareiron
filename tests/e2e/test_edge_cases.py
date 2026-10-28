import os
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402
from test_load import keep_alive_gap  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken


def wait_for(predicate, timeout=10.0):
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    if predicate():
      return True
    time.sleep(0.1)
  return False


class EdgeCaseTest(unittest.TestCase):

  def test_stalled_client_does_not_freeze_others(self):
    with ServerProcess(PORT) as server:
      stalled = Bot(port=PORT, name="stalled")
      stalled.login()
      # From here on the stalled bot never reads its socket again
      healthy = [Bot(port=PORT, name="ok%d" % i) for i in range(3)]
      for bot in healthy:
        bot.login()
      errors = []

      def run(bot):
        try:
          bot.walk_circle(35, radius=20)
        except Exception as error:
          errors.append("%s: %r" % (bot.name, error))

      threads = [threading.Thread(target=run, args=(bot,)) for bot in healthy]
      for thread in threads:
        thread.start()
      dropped = wait_for(lambda: any(e["type"] == "leave" and e["name"] == "stalled" for e in server.events()), 30)
      for thread in threads:
        thread.join()
      end = time.monotonic()
      self.assertTrue(dropped, "stalled client was never dropped")
      self.assertEqual(errors, [])
      for bot in healthy:
        self.assertLess(keep_alive_gap(bot, end), 20, bot.name)
        bot.close()
      stalled.close()

  def test_abrupt_close_during_join(self):
    with ServerProcess(PORT) as server:
      for i in range(10):
        bot = Bot(port=PORT, name="quitter")
        bot._connect()
        bot._handshake(2)
        import uuid
        bot.send(0x00, __import__("mcbot").string(bot.name) + uuid.uuid3(uuid.NAMESPACE_OID, "OfflinePlayer:quitter").bytes)
        bot.recv(timeout=5)  # Set Compression or Login Success
        bot.close(abrupt=True)
      time.sleep(1.5)
      self.assertEqual(server.status()["player_count"], 0)
      survivor = Bot(port=PORT, name="survivor")
      survivor.login()
      survivor.play_for(2)
      self.assertTrue(wait_for(lambda: server.status()["player_count"] == 1, 4))
      survivor.close()

  def test_rejoin_same_name_single_entry(self):
    with ServerProcess(PORT) as server:
      first = Bot(port=PORT, name="bot1")
      first.login()
      first.play_for(2)
      x, y, z = first.position
      for step in range(20):
        first.move_to(x + step + 1, y, z)
        first.pump(0.05)
      first.pump(1)
      first.close(abrupt=True)
      time.sleep(0.5)
      again = Bot(port=PORT, name="bot1")
      again.login()
      again.play_for(2)
      self.assertLess(abs(again.position[0] - (x + 20)), 2)
      self.assertTrue(wait_for(lambda: server.status()["player_count"] == 1, 4))
      names = [p["name"] for p in server.status()["players"]]
      self.assertEqual(names, ["bot1"])
      again.close()

  def test_rejoin_while_old_connection_hangs(self):
    # Like a phone that lost WiFi: the old socket stays open but silent
    with ServerProcess(PORT) as server:
      ghost = Bot(port=PORT, name="bot1")
      ghost.login()
      ghost.play_for(2)
      again = Bot(port=PORT, name="bot1")
      again.login()
      again.play_for(2)
      kinds = [(e["type"], e["name"]) for e in server.events() if e["type"] in ("join", "leave")]
      self.assertEqual(kinds, [("join", "bot1"), ("leave", "bot1"), ("join", "bot1")])
      self.assertTrue(wait_for(lambda: server.status()["player_count"] == 1, 4))
      self.assertEqual([p["name"] for p in server.status()["players"]], ["bot1"])
      # The server hangs up on the old connection instead of keeping it
      closed = False
      try:
        ghost.pump(2)
      except Exception:
        closed = True
      self.assertTrue(closed, "old connection still open")
      again.close()
      ghost.close()

  def test_status_pings_do_not_leak_slots(self):
    with ServerProcess(PORT) as server:
      for i in range(50):
        Bot(port=PORT, name="pinger").status()
      bots = [Bot(port=PORT, name="joiner%d" % i) for i in range(6)]
      for bot in bots:
        bot.login()
      for bot in bots:
        bot.play_for(0.5)
      self.assertTrue(wait_for(lambda: server.status()["player_count"] == 6, 6), server.status())
      for bot in bots:
        bot.close()


if __name__ == "__main__":
  unittest.main()
