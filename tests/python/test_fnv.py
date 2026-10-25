# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the FNV-1 hashes and of the screen clip key."""

import unittest

import support  # noqa: F401 - puts the mod on sys.path.
from ts4_youtube_tv import fnv
from ts4_youtube_tv import tv


class FnvTest(unittest.TestCase):
    def test_gives_the_key_of_a_game_clip(self):
        # The research checked this key in ClientFullBuild1.package of the game.
        self.assertEqual(fnv.fnv1_64("TS4_TV_Comedy_Standup"), 0x8A50833624769271)

    def test_hashes_the_name_in_lower_case(self):
        self.assertEqual(fnv.fnv1_64("TS4_TV_COMEDY_STANDUP"), fnv.fnv1_64("ts4_tv_comedy_standup"))
        self.assertNotEqual(fnv.fnv1_64("ABC", lower=False), fnv.fnv1_64("abc", lower=False))

    def test_gives_the_standard_values(self):
        # The FNV-1 values of the empty text and of "a" (the published test vectors).
        self.assertEqual(fnv.fnv1_64(""), 0xCBF29CE484222325)
        self.assertEqual(fnv.fnv1_64("a"), 0xAF63BD4C8601B7BE)
        self.assertEqual(fnv.fnv1_32(""), 0x811C9DC5)
        self.assertEqual(fnv.fnv1_32("a"), 0x050C5D7E)

    def test_makes_the_key_of_the_screen_clip(self):
        instance, resource_type, group = tv.clip_key("TS4_TV_Comedy_Standup")
        self.assertEqual((instance, resource_type, group), (0x8A50833624769271, 0x376840D7, 0))
        self.assertEqual(tv.clip_key("TS4_TV_Yulelog")[0], 0x0D432B7F5C14E35D)


if __name__ == "__main__":
    unittest.main()
