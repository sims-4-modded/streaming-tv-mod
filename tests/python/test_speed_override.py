# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the speed override: the TV of a session mutes and distorts only at the super speed, and the old values
come back at each end of the session."""

import unittest

import support
import zone
from objects.components import types
from ts4_youtube_tv import session
from ts4_youtube_tv import speed_override

URL = "https://www.youtube.com/watch?v=M7lc1UVf-VE"
SUPER = 5
OLD = 2


class SpeedOverrideTest(unittest.TestCase):
    def setUp(self):
        self.tv = support.FakeObject()
        self.other = support.FakeObject()
        self.sim = support.FakeSim()
        support.reset_game([self.tv, self.other])
        support.use_dll(support.FakeDll())
        self.component = self.tv.get_component(types.VIDEO_COMPONENT)
        self.other_component = self.other.get_component(types.VIDEO_COMPONENT)

    def tearDown(self):
        support.use_dll(None)
        session.clear()

    def start(self):
        ok, error = session.start(self.tv, URL, self.sim)
        self.assertTrue(ok, error)
        return session.sessions()[self.tv.id].interaction

    def speeds(self, component):
        return component.mute_speed, component.distortion_speed

    def last_message(self, obj=None):
        message = (obj or self.tv).sent_playlists[-1]
        return message["mute_speed"], message["distortion_speed"]

    def test_the_start_sets_the_super_speed(self):
        self.start()
        self.assertEqual(self.speeds(self.component), (SUPER, SUPER))
        self.assertTrue(speed_override.is_overridden(self.tv.id))

    def test_each_new_playlist_carries_the_super_speed(self):
        self.start()
        self.component.set_video_clips(["TS4_TV_News"], loop_last=True)
        self.assertEqual(self.last_message(), (SUPER, SUPER))
        self.component.add_video_clips(["TS4_TV_News_2"])
        self.assertEqual(len(self.tv.sent_playlists), 2)
        self.assertEqual(self.last_message(), (SUPER, SUPER))

    def test_the_start_sends_the_current_playlist_again(self):
        self.component.set_video_clips(["TS4_TV_News"], loop_last=True)
        self.assertEqual(self.last_message(), (OLD, OLD))
        version = self.tv.video_playlist.version_id
        self.start()
        self.assertEqual(len(self.tv.sent_playlists), 2)
        self.assertEqual(self.last_message(), (SUPER, SUPER))
        # The game sends the same playlist again, not a new one.
        self.assertEqual(self.tv.sent_playlists[-1]["version_id"], version)

    def test_no_playlist_sends_nothing_at_the_start(self):
        self.start()
        self.assertEqual(self.tv.sent_playlists, [])

    def test_the_end_of_the_watch_restores_the_values(self):
        interaction = self.start()
        self.component.set_video_clips(["TS4_TV_News"], loop_last=True)
        interaction.finish()
        self.assertEqual(self.speeds(self.component), (OLD, OLD))
        self.assertFalse(speed_override.is_overridden(self.tv.id))
        self.assertEqual(self.last_message(), (OLD, OLD))
        self.component.set_video_clips(["TS4_TV_News"], loop_last=True)
        self.assertEqual(self.last_message(), (OLD, OLD))

    def test_the_stop_command_restores_the_values(self):
        self.start()
        session.stop(self.tv.id)
        self.assertEqual(self.speeds(self.component), (OLD, OLD))

    def test_a_playlist_after_the_end_of_the_watch_restores_the_values(self):
        interaction = self.start()
        interaction.callbacks.clear()
        interaction.is_finishing = True
        self.component.set_video_clips()
        self.assertFalse(session.has_session(self.tv.id))
        self.assertEqual(self.speeds(self.component), (OLD, OLD))
        self.assertEqual(self.last_message(), (OLD, OLD))

    def test_the_removal_of_the_tv_restores_without_a_send(self):
        self.start()
        self.component.set_video_clips(["TS4_TV_News"], loop_last=True)
        sent = len(self.tv.sent_playlists)
        self.component.on_remove()
        self.assertEqual(self.speeds(self.component), (OLD, OLD))
        self.assertEqual(len(self.tv.sent_playlists), sent)
        self.assertEqual((self.tv.video_playlist.mute_speed, self.tv.video_playlist.distortion_speed), (OLD, OLD))

    def test_the_zone_unload_restores_without_a_send(self):
        self.start()
        self.component.set_video_clips(["TS4_TV_News"], loop_last=True)
        sent = len(self.tv.sent_playlists)
        zone.Zone().on_teardown("client")
        self.assertEqual(self.speeds(self.component), (OLD, OLD))
        self.assertEqual(len(self.tv.sent_playlists), sent)
        self.assertFalse(speed_override.is_overridden(self.tv.id))

    def test_the_zone_unload_restores_a_tv_that_the_object_manager_lost(self):
        self.start()
        support.services.objects = support.FakeObjectManager([])
        zone.Zone().on_teardown("client")
        self.assertEqual(self.speeds(self.component), (OLD, OLD))

    def test_a_new_video_keeps_the_old_values(self):
        self.component.set_video_clips(["TS4_TV_News"], loop_last=True)
        first = self.start()
        sent = len(self.tv.sent_playlists)
        self.start()
        # The replace sends the playlist one time, with the super speed.
        self.assertEqual(len(self.tv.sent_playlists), sent + 1)
        self.assertEqual(self.last_message(), (SUPER, SUPER))
        first.finish()
        self.assertEqual(self.speeds(self.component), (SUPER, SUPER))
        session.sessions()[self.tv.id].interaction.finish()
        self.assertEqual(self.speeds(self.component), (OLD, OLD))

    def test_other_tvs_do_not_change(self):
        self.other_component.set_video_clips(["TS4_TV_News"], loop_last=True)
        self.start()
        self.other_component.set_video_clips(["TS4_TV_Action"], loop_last=True)
        self.assertEqual(self.speeds(self.other_component), (OLD, OLD))
        self.assertEqual(self.last_message(self.other), (OLD, OLD))
        self.assertEqual(len(self.other.sent_playlists), 2)

    def test_a_refused_video_changes_nothing(self):
        support.use_dll(support.FakeDll(play_result=0, error=b"no helper"))
        ok, _ = session.start(self.tv, URL, self.sim)
        self.assertFalse(ok)
        self.assertEqual(self.speeds(self.component), (OLD, OLD))
        self.assertFalse(speed_override.is_overridden(self.tv.id))


if __name__ == "__main__":
    unittest.main()
