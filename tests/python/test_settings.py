# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the settings of the script mod."""

import unittest

import support  # noqa: F401 - puts the mod and the fakes on the path.
from ts4_youtube_tv import settings


class SettingsTest(unittest.TestCase):
    def test_defaults(self):
        values = settings.parse({})
        self.assertEqual(values["watch_affordances"], ("tv_WatchNews", "tv_WatchAction", "tv_WatchMusic"))
        self.assertTrue(values["enabled"])

    def test_reads_the_watch_interactions(self):
        values = settings.parse({"watch_affordances": ["tv_WatchFireplace", "tv_WatchNews"]})
        self.assertEqual(values["watch_affordances"], ("tv_WatchFireplace", "tv_WatchNews"))

    def test_keeps_the_default_for_incorrect_names(self):
        for value in ("tv_WatchNews", [], ["tv Watch"], [1], ["a"] * 17):
            values = settings.parse({"watch_affordances": value})
            self.assertEqual(values["watch_affordances"], settings.DEFAULT_WATCH_AFFORDANCES, value)

    def test_the_old_field_screen_clip_is_not_known(self):
        values = settings.parse({"screen_clip": "TS4_TV_Yulelog"})
        self.assertNotIn("screen_clip", values)


if __name__ == "__main__":
    unittest.main()
