# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the sessions: the start, the stop on a foreign playlist, the removal and the zone unload."""

import unittest

import support
import zone
from objects.components import types
from objects.components.video import VideoComponent
from ts4_youtube_tv import session
from ts4_youtube_tv import settings

URL = "https://www.youtube.com/watch?v=M7lc1UVf-VE&t=1m"
YULE_LOG_KEY = (0x0D432B7F5C14E35D, 0x376840D7, 0)


class SessionTest(unittest.TestCase):
    def setUp(self):
        self.tv = support.FakeObject()
        support.reset_game([self.tv])
        self.dll = support.use_dll(support.FakeDll())
        self.component = self.tv.get_component(types.VIDEO_COMPONENT)

    def tearDown(self):
        support.use_dll(None)
        session.clear()

    def test_the_start_wraps_the_video_component_and_the_zone(self):
        for name in ("set_video_clips", "add_video_clips", "on_remove"):
            self.assertTrue(getattr(getattr(VideoComponent, name), "_ts4_youtube_tv_wrapped", False), name)
        self.assertTrue(getattr(zone.Zone.on_teardown, "_ts4_youtube_tv_wrapped", False))
        wrapped = VideoComponent.set_video_clips
        session.install()
        self.assertIs(VideoComponent.set_video_clips, wrapped)

    def test_starts_a_video(self):
        ok, error = session.start(self.tv, URL)
        self.assertTrue(ok, error)
        self.assertEqual(self.component.calls, [("set", ["TS4_TV_Yulelog"], True)])
        self.assertEqual(self.dll.YoutubeTvSetScreenKey.calls, [(self.tv.id,) + YULE_LOG_KEY])
        object_id, url, position, orientation = self.dll.YoutubeTvPlay.calls[0]
        self.assertEqual((object_id, url), (self.tv.id, URL.encode("utf-8")))
        self.assertEqual(list(position), [1.0, 2.0, 3.0])
        self.assertEqual(list(orientation), [0.0, 0.5, 0.0, 0.5])
        # Our own call to set_video_clips does not stop the session.
        self.assertTrue(session.has_session(self.tv.id))
        self.assertEqual(self.dll.YoutubeTvStop.calls, [])

    def test_uses_the_clip_of_the_settings(self):
        settings.set_values({"screen_clip": "TS4_TV_Comedy_Standup"})
        session.start(self.tv, URL)
        self.assertEqual(self.component.calls[0], ("set", ["TS4_TV_Comedy_Standup"], True))
        self.assertEqual(self.dll.YoutubeTvSetScreenKey.calls[0][1], 0x8A50833624769271)

    def test_stops_on_a_foreign_playlist(self):
        session.start(self.tv, URL)
        self.component.set_video_clips(["TS4_TV_Comedy_Standup"], loop_last=True)
        self.assertFalse(session.has_session(self.tv.id))
        self.assertEqual(self.dll.YoutubeTvStop.calls, [(self.tv.id,)])
        # The original ran first and kept the playlist of the game.
        self.assertEqual(self.tv.video_playlist, ["TS4_TV_Comedy_Standup"])
        self.assertEqual(self.tv.location_callbacks, [])

    def test_stops_when_the_tv_goes_off(self):
        session.start(self.tv, URL)
        self.component.set_video_clips()
        self.assertFalse(session.has_session(self.tv.id))

    def test_stops_on_added_clips(self):
        session.start(self.tv, URL)
        self.component.add_video_clips(["TS4_TV_Action"])
        self.assertFalse(session.has_session(self.tv.id))

    def test_a_foreign_playlist_on_another_tv_does_not_stop_the_session(self):
        other = support.FakeObject()
        session.start(self.tv, URL)
        other.get_component(types.VIDEO_COMPONENT).set_video_clips(["TS4_TV_Action"])
        self.assertTrue(session.has_session(self.tv.id))

    def test_stops_when_the_game_removes_the_component(self):
        session.start(self.tv, URL)
        self.component.on_remove()
        self.assertFalse(session.has_session(self.tv.id))
        self.assertEqual(self.component.calls[-1], ("remove",))

    def test_stops_all_on_zone_unload(self):
        second = support.FakeObject()
        support.services.objects = support.FakeObjectManager([self.tv, second])
        session.start(self.tv, URL)
        session.start(second, URL)
        zone.Zone.teardown_calls.clear()
        zone.Zone().on_teardown("client")
        self.assertEqual(session.sessions(), {})
        self.assertEqual(len(self.dll.YoutubeTvStopAll.calls), 1)
        self.assertEqual(zone.Zone.teardown_calls, ["client"])

    def test_zone_unload_calls_the_original_after_an_error(self):
        session.start(self.tv, URL)
        self.dll.YoutubeTvStopAll.side_effect = lambda: 1 / 0
        zone.Zone.teardown_calls.clear()
        zone.Zone().on_teardown("client")
        self.assertEqual(zone.Zone.teardown_calls, ["client"])

    def test_sends_the_transform_when_the_tv_moves(self):
        session.start(self.tv, URL)
        self.tv.move(7.0, 8.0, 9.0)
        object_id, position, orientation = self.dll.YoutubeTvSetTransform.calls[0]
        self.assertEqual(object_id, self.tv.id)
        self.assertEqual(list(position), [7.0, 8.0, 9.0])

    def test_clears_the_screen_if_the_dll_refuses(self):
        self.dll.YoutubeTvPlay.result = 0
        self.dll.error = b"no helper"
        ok, error = session.start(self.tv, URL)
        self.assertFalse(ok)
        self.assertEqual(error, "no helper")
        self.assertEqual(self.component.calls[-1], ("set", [], False))
        self.assertFalse(session.has_session(self.tv.id))

    def test_refuses_without_the_dll(self):
        support.use_dll(None)
        ok, error = session.start(self.tv, URL)
        self.assertFalse(ok)
        self.assertIn("not ready", error)
        self.assertEqual(self.component.calls, [])

    def test_a_new_video_replaces_the_old_one(self):
        session.start(self.tv, URL)
        session.start(self.tv, "https://youtu.be/dQw4w9WgXcQ")
        self.assertEqual(session.sessions()[self.tv.id].url, "https://youtu.be/dQw4w9WgXcQ")
        self.assertEqual(len(self.tv.location_callbacks), 1)

    def test_the_stop_command_clears_the_screen(self):
        session.start(self.tv, URL)
        self.assertTrue(session.stop(self.tv.id))
        self.assertEqual(self.component.calls[-1], ("set", [], False))
        self.assertFalse(session.stop(self.tv.id))


if __name__ == "__main__":
    unittest.main()
