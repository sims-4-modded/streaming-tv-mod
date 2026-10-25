# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the sessions: the Sim watches, the stop at the end of the watch, the playlists, the removal, the zone
unload and the channel sound."""

import unittest

import support
import zone
from objects.components import types
from objects.components.state import StateComponentManagedDistributables
from objects.components.video import VideoComponent
from ts4_youtube_tv import channel_audio
from ts4_youtube_tv import session
from ts4_youtube_tv import settings

URL = "https://www.youtube.com/watch?v=M7lc1UVf-VE&t=1m"


class SessionTest(unittest.TestCase):
    def setUp(self):
        self.tv = support.FakeObject()
        self.sim = support.FakeSim()
        support.reset_game([self.tv])
        self.dll = support.use_dll(support.FakeDll())
        self.component = self.tv.get_component(types.VIDEO_COMPONENT)

    def tearDown(self):
        support.use_dll(None)
        session.clear()

    def start(self, obj=None, url=URL):
        ok, error = session.start(obj or self.tv, url, self.sim)
        self.assertTrue(ok, error)
        return session.sessions()[(obj or self.tv).id].interaction

    def test_the_start_wraps_the_game(self):
        for name in ("set_video_clips", "add_video_clips", "on_remove"):
            self.assertTrue(getattr(getattr(VideoComponent, name), "_ts4_youtube_tv_wrapped", False), name)
        self.assertTrue(getattr(zone.Zone.on_teardown, "_ts4_youtube_tv_wrapped", False))
        self.assertTrue(getattr(StateComponentManagedDistributables.apply, "_ts4_youtube_tv_wrapped", False))
        wrapped = VideoComponent.set_video_clips
        session.install()
        self.assertIs(VideoComponent.set_video_clips, wrapped)

    def test_the_sim_watches_and_the_video_starts(self):
        interaction = self.start()
        affordance, target, context = self.sim.pushed[0]
        self.assertIs(affordance, support.tv_WatchNews)
        self.assertIs(target, self.tv)
        self.assertEqual(context.source, "script_with_user_intent")
        self.assertIs(interaction.affordance, support.tv_WatchNews)
        object_id, url, position, orientation = self.dll.YoutubeTvPlay.calls[0]
        self.assertEqual((object_id, url), (self.tv.id, URL.encode("utf-8")))
        self.assertEqual(list(position), [1.0, 2.0, 3.0])
        self.assertEqual(list(orientation), [0.0, 0.5, 0.0, 0.5])
        # The mod does not put its own clip on the TV: the channel of the watch interaction sets the playlist.
        self.assertEqual(self.component.calls, [])
        self.assertEqual(self.dll.YoutubeTvSetScreenKey.calls, [])

    def test_the_playlist_of_the_watch_does_not_stop_the_session(self):
        self.start()
        self.component.set_video_clips(["TS4_TV_News"], loop_last=True)
        self.component.add_video_clips(["TS4_TV_News_2"])
        self.assertTrue(session.has_session(self.tv.id))
        self.assertEqual(self.dll.YoutubeTvStop.calls, [])

    def test_the_end_of_the_watch_stops_the_session(self):
        interaction = self.start()
        interaction.finish()
        self.assertFalse(session.has_session(self.tv.id))
        self.assertEqual(self.dll.YoutubeTvStop.calls, [(self.tv.id,)])
        self.assertEqual(interaction.callbacks, [])
        self.assertEqual(self.tv.location_callbacks, [])

    def test_a_playlist_after_the_end_of_the_watch_stops_the_session(self):
        interaction = self.start()
        # The finishing callback is gone, for example after a reload of the mod.
        interaction.callbacks.clear()
        interaction.is_finishing = True
        self.component.set_video_clips()
        self.assertFalse(session.has_session(self.tv.id))

    def test_the_sim_cannot_watch(self):
        self.sim.accepts = False
        ok, error = session.start(self.tv, URL, self.sim)
        self.assertFalse(ok)
        self.assertIn("cannot watch", error)
        self.assertEqual(self.dll.YoutubeTvPlay.calls, [])
        self.assertFalse(session.has_session(self.tv.id))

    def test_no_sim(self):
        ok, error = session.start(self.tv, URL, None)
        self.assertFalse(ok)
        self.assertIn("no Sim", error)

    def test_a_tv_without_a_watch_interaction(self):
        self.tv.affordances = [support.tv_ChannelSurf]
        ok, error = session.start(self.tv, URL, self.sim)
        self.assertFalse(ok)
        self.assertIn("no watch interaction", error)

    def test_a_refusal_of_the_dll_cancels_the_watch(self):
        self.dll.YoutubeTvPlay.result = 0
        self.dll.error = b"no helper"
        ok, error = session.start(self.tv, URL, self.sim)
        self.assertFalse(ok)
        self.assertEqual(error, "no helper")
        self.assertFalse(session.has_session(self.tv.id))
        self.assertEqual(len(self.sim.interactions[0].cancels), 1)

    def test_refuses_without_the_dll(self):
        support.use_dll(None)
        ok, error = session.start(self.tv, URL, self.sim)
        self.assertFalse(ok)
        self.assertIn("not ready", error)
        self.assertEqual(self.sim.pushed, [])

    def test_the_stop_command_cancels_the_watch(self):
        interaction = self.start()
        self.assertTrue(session.stop(self.tv.id))
        self.assertEqual(len(interaction.cancels), 1)
        self.assertEqual(interaction.cancels[0][0], "user_cancel")
        self.assertFalse(session.stop(self.tv.id))
        self.assertEqual(self.dll.YoutubeTvStop.calls, [(self.tv.id,)])

    def test_a_new_video_replaces_the_old_one(self):
        first = self.start()
        second = self.start(url="https://youtu.be/dQw4w9WgXcQ")
        self.assertIsNot(first, second)
        self.assertEqual(session.sessions()[self.tv.id].url, "https://youtu.be/dQw4w9WgXcQ")
        self.assertEqual(len(self.tv.location_callbacks), 1)
        # The end of the first watch does not stop the second video.
        first.finish()
        self.assertTrue(session.has_session(self.tv.id))

    def test_stops_when_the_game_removes_the_component(self):
        self.start()
        self.component.on_remove()
        self.assertFalse(session.has_session(self.tv.id))
        self.assertEqual(self.component.calls[-1], ("remove",))

    def test_stops_all_on_zone_unload(self):
        second = support.FakeObject()
        support.services.objects = support.FakeObjectManager([self.tv, second])
        self.start()
        self.start(second)
        zone.Zone.teardown_calls.clear()
        zone.Zone().on_teardown("client")
        self.assertEqual(session.sessions(), {})
        self.assertEqual(len(self.dll.YoutubeTvStopAll.calls), 1)
        self.assertEqual(zone.Zone.teardown_calls, ["client"])

    def test_zone_unload_calls_the_original_after_an_error(self):
        self.start()
        self.dll.YoutubeTvStopAll.side_effect = lambda: 1 / 0
        zone.Zone.teardown_calls.clear()
        zone.Zone().on_teardown("client")
        self.assertEqual(zone.Zone.teardown_calls, ["client"])

    def test_sends_the_transform_when_the_tv_moves(self):
        self.start()
        self.tv.move(7.0, 8.0, 9.0)
        object_id, position, orientation = self.dll.YoutubeTvSetTransform.calls[0]
        self.assertEqual(object_id, self.tv.id)
        self.assertEqual(list(position), [7.0, 8.0, 9.0])

    def test_uses_the_watch_interaction_of_the_settings(self):
        self.tv.affordances = [support.tv_WatchNews, support.tv_WatchFireplace]
        settings.set_values({"watch_affordances": ("tv_WatchFireplace", "tv_WatchNews")})
        try:
            self.start()
            self.assertIs(self.sim.pushed[0][0], support.tv_WatchFireplace)
        finally:
            settings.set_values({})

    def test_takes_the_first_watch_interaction_if_no_name_matches(self):
        self.tv.affordances = [support.tv_ChannelSurf, support.tv_WatchFireplace]
        self.start()
        self.assertIs(self.sim.pushed[0][0], support.tv_WatchFireplace)


class ChannelSoundTest(unittest.TestCase):
    def setUp(self):
        self.tv = support.FakeObject()
        self.other = support.FakeObject()
        self.sim = support.FakeSim()
        support.reset_game([self.tv, self.other])
        support.use_dll(support.FakeDll())

    def tearDown(self):
        support.use_dll(None)
        session.clear()

    def test_silences_the_sound_that_plays_at_the_start(self):
        sound = self.tv.set_channel_sound()
        self.assertTrue(sound.playing)
        session.start(self.tv, URL, self.sim)
        self.assertFalse(sound.playing)
        self.assertTrue(channel_audio.is_silenced(self.tv.id))

    def test_silences_the_sound_of_a_new_channel(self):
        session.start(self.tv, URL, self.sim)
        sound = self.tv.set_channel_sound()
        self.assertFalse(sound.playing)
        self.assertEqual(sound.starts, 1)
        self.assertEqual(sound.stops, 1)
        # The game keeps the sound in its map, so it can stop or replace it as usual.
        managed = self.tv.state_component._custom_distributables
        self.assertIs(managed._distributables_map[("audio_state", support.FakeState)], sound)

    def test_does_not_silence_another_tv(self):
        session.start(self.tv, URL, self.sim)
        sound = self.other.set_channel_sound()
        self.assertTrue(sound.playing)

    def test_starts_the_sound_again_at_the_stop(self):
        sound = self.tv.set_channel_sound()
        session.start(self.tv, URL, self.sim)
        session.sessions()[self.tv.id].interaction.finish()
        self.assertTrue(sound.playing)
        self.assertFalse(channel_audio.is_silenced(self.tv.id))
        new_sound = self.tv.set_channel_sound()
        self.assertTrue(new_sound.playing)

    def test_other_changes_play(self):
        session.start(self.tv, URL, self.sim)
        managed = self.tv.state_component._custom_distributables
        managed.apply(self.tv, "vfx", support.FakeState, None, support.FakeSound)
        self.assertTrue(managed._distributables_map[("vfx", support.FakeState)].playing)


if __name__ == "__main__":
    unittest.main()
