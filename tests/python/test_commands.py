# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the cheat commands."""

import unittest

import support
import sims4.commands
from ts4_youtube_tv import session

URL = "https://youtu.be/M7lc1UVf-VE?t=10"


def command(name):
    function, command_type = sims4.commands.registry[name]
    assert command_type == sims4.commands.CommandType.Live
    return function


class CommandTest(unittest.TestCase):
    def setUp(self):
        self.near = support.FakeObject(position=(1.0, 0.0, 1.0))
        self.far = support.FakeObject(position=(50.0, 0.0, 50.0))
        self.broken = support.FakeObject(position=(0.0, 0.0, 0.0), usable=False)
        self.sim = support.FakeSim()
        support.reset_game([self.far, self.near, self.broken], active_sim=self.sim)
        self.dll = support.use_dll(support.FakeDll())
        sims4.commands.outputs.clear()

    def tearDown(self):
        support.use_dll(None)
        session.clear()

    def test_registers_the_commands_with_the_mod_prefix(self):
        for name in ("youtube_tv.play", "youtube_tv.stop", "youtube_tv.status", "youtube_tv.info"):
            self.assertIn(name, sims4.commands.registry)

    def test_play_uses_the_nearest_tv_that_works(self):
        self.assertTrue(command("youtube_tv.play")(URL, _connection=1))
        self.assertTrue(session.has_session(self.near.id))
        self.assertEqual(self.dll.YoutubeTvPlay.calls[0][1], URL.encode("utf-8"))
        # The active Sim watches the TV.
        affordance, target, _ = self.sim.pushed[0]
        self.assertIs(target, self.near)
        self.assertIs(affordance, support.tv_WatchNews)

    def test_play_refuses_an_incorrect_url(self):
        self.assertFalse(command("youtube_tv.play")("https://example.com", _connection=1))
        self.assertIn("not the URL", sims4.commands.outputs[-1])

    def test_play_without_a_url_shows_the_usage(self):
        self.assertFalse(command("youtube_tv.play")(_connection=1))
        self.assertIn("Usage", sims4.commands.outputs[-1])

    def test_play_reports_a_lot_without_a_tv(self):
        support.reset_game([], active_sim=self.sim)
        self.assertFalse(command("youtube_tv.play")(URL, _connection=1))
        self.assertIn("no TV", sims4.commands.outputs[-1])

    def test_stop_stops_every_video(self):
        command("youtube_tv.play")(URL, _connection=1)
        self.assertEqual(command("youtube_tv.stop")(_connection=1), 1)
        self.assertEqual(session.sessions(), {})

    def test_status_and_info_show_the_state(self):
        command("youtube_tv.play")(URL, _connection=1)
        self.assertTrue(command("youtube_tv.status")(_connection=1))
        text = "\n".join(sims4.commands.outputs)
        self.assertIn("API version 1", text)
        self.assertIn("watch_affordances=tv_WatchNews, tv_WatchAction, tv_WatchMusic", text)
        self.assertTrue(command("youtube_tv.info")(_connection=1))
        text = "\n".join(sims4.commands.outputs)
        self.assertIn("mute_speed = 3", text)
        self.assertIn("distortion_speed = 3", text)
        self.assertIn("session = True", text)

    def test_a_command_never_raises(self):
        support.services.objects = None
        support.services.active_sim = None

        class Broken:
            def valid_objects(self):
                raise RuntimeError("no zone")
        support.services.objects = Broken()
        self.assertFalse(command("youtube_tv.info")(_connection=1))
        self.assertIn("failed", sims4.commands.outputs[-1])


if __name__ == "__main__":
    unittest.main()
