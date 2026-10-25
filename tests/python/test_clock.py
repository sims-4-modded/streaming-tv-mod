# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the wrap of the game clock."""

import unittest

import support
import clock as game_clock
from ts4_youtube_tv import clock


class ClockTest(unittest.TestCase):
    def setUp(self):
        self.dll = support.use_dll(support.FakeDll())

    def tearDown(self):
        support.use_dll(None)

    def test_the_start_wraps_the_sync_once(self):
        wrapped = game_clock.GameClock._sync_clock_and_broadcast_gameclock
        self.assertTrue(getattr(wrapped, "_ts4_youtube_tv_wrapped", False))
        self.assertTrue(clock.install())
        self.assertIs(game_clock.GameClock._sync_clock_and_broadcast_gameclock, wrapped)
        self.assertEqual(wrapped.__name__, "_sync_clock_and_broadcast_gameclock")

    def test_calls_the_original_first(self):
        order = []
        game = game_clock.GameClock()
        game.events = order
        self.dll.YoutubeTvSetSpeed.side_effect = lambda *args: order.append(("speed",) + args)
        game.clock_speed = game_clock.ClockSpeedMode.NORMAL
        game._sync_clock_and_broadcast_gameclock(immediate=True)
        self.assertEqual(order, [("sync", 1, True), ("speed", 1, 1.0, 0)])

    def test_maps_each_mode_to_set_speed(self):
        game = game_clock.GameClock()
        expected = {
            game_clock.ClockSpeedMode.PAUSED: (0, 0.0, 0),
            game_clock.ClockSpeedMode.NORMAL: (1, 1.0, 0),
            game_clock.ClockSpeedMode.SPEED2: (2, 3.0, 0),
            game_clock.ClockSpeedMode.SPEED3: (3, 7.0, 0),
            game_clock.ClockSpeedMode.INTERACTION_STARTUP_SPEED: (4, 3.5, 0),
            game_clock.ClockSpeedMode.SUPER_SPEED3: (5, 25.0, 1),
        }
        for mode, values in expected.items():
            self.dll.YoutubeTvSetSpeed.calls.clear()
            game.clock_speed = mode
            game._sync_clock_and_broadcast_gameclock()
            self.assertEqual(self.dll.YoutubeTvSetSpeed.calls, [values], mode)
        self.assertEqual(clock.last_sent, (5, 25.0, True))

    def test_an_error_does_not_stop_the_game(self):
        game = game_clock.GameClock()
        game.current_clock_speed_scale = None  # The wrap cannot read the scale.
        game._sync_clock_and_broadcast_gameclock()
        self.assertEqual(game.events, [("sync", 0, False)])

    def test_works_without_the_dll(self):
        support.use_dll(None)
        game = game_clock.GameClock()
        game._sync_clock_and_broadcast_gameclock()
        self.assertEqual(len(game.events), 1)


if __name__ == "__main__":
    unittest.main()
