import os
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = 25565


def wait_for(predicate, timeout=8.0):
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    if predicate():
      return True
    time.sleep(0.05)
  return False


class StatusApiTest(unittest.TestCase):

  def test_join_leave_events(self):
    with ServerProcess(PORT) as server:
      bot = Bot(port=PORT, name="bot1")
      bot.login()
      bot.play_for(4)
      bot.close()
      self.assertTrue(wait_for(lambda: any(e["type"] == "leave" for e in server.events())), server.log[-2000:])
      kinds = [(e["type"], e.get("name")) for e in server.events() if e["type"] in ("join", "leave")]
      self.assertEqual(kinds, [("join", "bot1"), ("leave", "bot1")])
      self.assertEqual(server.events()[0]["type"], "ready")

  def test_status_lists_players(self):
    with ServerProcess(PORT) as server:
      bots = [Bot(port=PORT, name="alpha"), Bot(port=PORT, name="beta")]
      for bot in bots:
        bot.login()
      for bot in bots:
        bot.play_for(2)
      self.assertTrue(wait_for(lambda: server.status()["player_count"] == 2))
      status = server.status()
      names = sorted(p["name"] for p in status["players"])
      self.assertEqual(names, ["alpha", "beta"])
      spawn_x, _, spawn_z = bots[0].position
      for p in status["players"]:
        self.assertLess(abs(p["x"] - spawn_x) + abs(p["z"] - spawn_z), 16)
      self.assertTrue(status["running"])
      for bot in bots:
        bot.close()

  def test_server_list_shows_count(self):
    with ServerProcess(PORT):
      bot = Bot(port=PORT, name="online")
      bot.login()
      bot.play_for(4)
      info = Bot(port=PORT, name="pinger").status()
      self.assertEqual(info["players"]["online"], 1)
      self.assertEqual(info["players"]["max"], 8)
      bot.close()


if __name__ == "__main__":
  unittest.main()
