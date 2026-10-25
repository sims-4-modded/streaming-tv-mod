# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the URL check. The examples are the same as in tests/test_YoutubeUrl.cpp of the DLL mod."""

import unittest

import support  # noqa: F401 - puts the mod on sys.path.
from ts4_youtube_tv import urls

ID = "M7lc1UVf-VE"


class UrlTest(unittest.TestCase):
    def expect_id(self, text, video_id=ID, start=0):
        video = urls.parse_youtube_video(text)
        self.assertEqual(video.id, video_id, text)
        self.assertEqual(video.start_seconds, start, text)
        self.assertTrue(urls.is_youtube_video_url(text), text)

    def expect_refused(self, text):
        with self.assertRaises(urls.UrlError, msg=text):
            urls.parse_youtube_video(text)
        self.assertFalse(urls.is_youtube_video_url(text), text)

    def test_accepts_a_correct_video_id(self):
        self.assertTrue(urls.is_video_id("M7lc1UVf-VE"))
        self.assertTrue(urls.is_video_id("dQw4w9WgXcQ"))
        self.assertTrue(urls.is_video_id("a_b-C1234_9"))
        self.assertFalse(urls.is_video_id("M7lc1UVf-V"))
        self.assertFalse(urls.is_video_id("M7lc1UVf-VEE"))
        self.assertFalse(urls.is_video_id("M7lc1UVf VE"))
        self.assertFalse(urls.is_video_id('M7lc1UVf"VE'))
        self.assertFalse(urls.is_video_id("M7lc1UVféVE"))
        self.expect_id("M7lc1UVf-VE")
        self.expect_id("  M7lc1UVf-VE \n")

    def test_reads_the_watch_page(self):
        self.expect_id("https://www.youtube.com/watch?v=M7lc1UVf-VE")
        self.expect_id("http://youtube.com/watch?v=M7lc1UVf-VE")
        self.expect_id("www.youtube.com/watch?v=M7lc1UVf-VE")
        self.expect_id("HTTPS://WWW.YOUTUBE.COM/watch?v=M7lc1UVf-VE")
        self.expect_id("https://m.youtube.com/watch?v=M7lc1UVf-VE")
        self.expect_id("https://music.youtube.com/watch?v=M7lc1UVf-VE")

    def test_reads_the_watch_page_with_other_parameters(self):
        self.expect_id("https://www.youtube.com/watch?feature=share&v=M7lc1UVf-VE&list=PL123&index=2")
        self.expect_id("https://www.youtube.com/watch?v=M7lc1UVf-VE&ab_channel=YouTubeDevelopers")
        self.expect_id("https://www.youtube.com/watch?v=M7lc1UVf-VE#comments")
        self.expect_id("https://www.youtube.com/watch?v=M7lc1UVf-VE&t=90", ID, 90)
        self.expect_id("https://www.youtube.com/watch?v=M7lc1UVf-VE&t=1m30s", ID, 90)

    def test_reads_the_short_link(self):
        self.expect_id("https://youtu.be/M7lc1UVf-VE")
        self.expect_id("youtu.be/M7lc1UVf-VE/")
        self.expect_id("https://youtu.be/M7lc1UVf-VE?si=AbCdEf123&t=42", ID, 42)

    def test_reads_shorts_embed_and_live(self):
        self.expect_id("https://www.youtube.com/shorts/M7lc1UVf-VE")
        self.expect_id("https://youtube.com/shorts/M7lc1UVf-VE?feature=share")
        self.expect_id("https://www.youtube.com/embed/M7lc1UVf-VE?start=10&autoplay=1", ID, 10)
        self.expect_id("https://www.youtube-nocookie.com/embed/M7lc1UVf-VE")
        self.expect_id("https://www.youtube.com/live/M7lc1UVf-VE?si=x")
        self.expect_id("https://www.youtube.com/v/M7lc1UVf-VE")

    def test_ignores_a_start_time_that_is_not_correct(self):
        self.expect_id("https://youtu.be/M7lc1UVf-VE?t=abc", ID, 0)

    def test_refuses_other_sites(self):
        self.expect_refused("https://vimeo.com/123456789")
        self.expect_refused("https://www.youtube.com.example.com/watch?v=M7lc1UVf-VE")
        self.expect_refused("https://notyoutube.com/watch?v=M7lc1UVf-VE")
        self.expect_refused("https://user@www.youtube.com/watch?v=M7lc1UVf-VE")
        self.expect_refused("https://www.youtube.com:8080/watch?v=M7lc1UVf-VE")
        self.expect_refused("ftp://youtu.be/M7lc1UVf-VE")

    def test_refuses_pages_without_a_video(self):
        self.expect_refused("")
        self.expect_refused("   ")
        self.expect_refused("https://www.youtube.com/")
        self.expect_refused("https://www.youtube.com/playlist?list=PL590L5WQmH8fJ54F369BLDSqIwcs-TCfs")
        self.expect_refused("https://www.youtube.com/@YouTubeDevelopers")
        self.expect_refused("https://www.youtube.com/channel/UC_x5XG1OV2P6uZZ5FSM9Ttw")
        self.expect_refused("https://www.youtube.com/watch?v=short")
        self.expect_refused("https://www.youtube.com/watch?vv=M7lc1UVf-VE")
        self.expect_refused("https://youtu.be/")
        self.expect_refused("https://youtu.be/M7lc1UVf-VE/extra")
        self.expect_refused("https://www.youtube.com/shorts/")

    def test_refuses_text_that_is_not_text(self):
        self.expect_refused(None)

    def test_reads_the_start_time(self):
        self.assertEqual(urls.parse_start_time("0"), 0)
        self.assertEqual(urls.parse_start_time("90"), 90)
        self.assertEqual(urls.parse_start_time("90s"), 90)
        self.assertEqual(urls.parse_start_time("2m"), 120)
        self.assertEqual(urls.parse_start_time("1h2m3s"), 3723)
        for text in ("", "m", "1x", "-5", "99999999999"):
            with self.assertRaises(urls.UrlError, msg=text):
                urls.parse_start_time(text)


if __name__ == "__main__":
    unittest.main()
