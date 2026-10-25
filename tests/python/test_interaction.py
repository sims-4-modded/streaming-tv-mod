# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the interaction: the test of the target, the dialog, and OK or Cancel."""

import unittest

import support
import tag
from sims4.localization import RawText
from ui import ui_dialog_generic
from ui import ui_dialog_notification
from ts4_youtube_tv import fnv
from ts4_youtube_tv import interaction
from ts4_youtube_tv import session
from ts4_youtube_tv import settings

URL = "https://www.youtube.com/watch?v=M7lc1UVf-VE&list=PL1#t=3"
Interaction = interaction.WatchYoutubeVideoInteraction


def run(instance):
    """Run the generator of the interaction to its end, as the game does."""
    generator = instance._run_interaction_gen(None)
    try:
        while True:
            next(generator)
    except StopIteration as stop:
        return stop.value


class InteractionTest(unittest.TestCase):
    def setUp(self):
        self.tv = support.FakeObject()
        self.sim = support.FakeSim()
        support.reset_game([self.tv])
        self.dll = support.use_dll(support.FakeDll())
        ui_dialog_generic.shown.clear()
        ui_dialog_notification.shown.clear()

    def tearDown(self):
        support.use_dll(None)
        session.clear()

    def show_dialog(self):
        self.assertTrue(run(Interaction(self.tv, self.sim)))
        self.assertEqual(len(ui_dialog_generic.shown), 1)
        return ui_dialog_generic.shown[0]

    def test_the_instance_id_is_the_hash_of_the_tuning_name(self):
        self.assertEqual(interaction.INSTANCE_ID, fnv.fnv1_64("ts4_youtube_tv:WatchYoutubeVideo"))

    def test_passes_only_for_a_tv_that_works(self):
        self.assertTrue(Interaction._test(self.tv, None))
        self.assertTrue(Interaction._test(support.FakeObject(tags=(tag.Tag.Func_Television,)), None))
        self.assertFalse(Interaction._test(support.FakeObject(tags=(tag.Tag.Func_Computer,)), None))
        self.assertFalse(Interaction._test(support.FakeObject(video=False), None))
        self.assertFalse(Interaction._test(support.FakeObject(usable=False), None))
        self.assertFalse(Interaction._test(None, None))

    def test_fails_without_the_dll_or_when_off(self):
        support.use_dll(None)
        self.assertFalse(Interaction._test(self.tv, None))
        support.use_dll(support.FakeDll())
        settings.set_values({"enabled": False})
        self.assertFalse(Interaction._test(self.tv, None))

    def test_has_a_raw_text_name(self):
        self.assertEqual(Interaction._get_name(target=self.tv), RawText("Watch a YouTube video"))

    def test_shows_the_dialog_with_raw_text_and_200_characters(self):
        dialog = self.show_dialog()
        self.assertIs(dialog.owner, self.sim)
        self.assertEqual(dialog.kwargs["title"](), RawText("Watch a YouTube video"))
        self.assertIsInstance(dialog.kwargs["text"](), RawText)
        text_input = dialog.kwargs["text_inputs"].url
        self.assertEqual(text_input.length_restriction.max_length, 200)
        self.assertIsNone(text_input.length_restriction.min_length)
        self.assertFalse(text_input.check_profanity)
        self.assertEqual([name for name, _ in dialog.kwargs["text_inputs"].items()], ["url"])

    def test_ok_with_a_correct_url_starts_the_video(self):
        dialog = self.show_dialog()
        dialog.respond(ui_dialog_generic.OK, "  " + URL + " ")
        self.assertTrue(session.has_session(self.tv.id))
        self.assertEqual(self.dll.YoutubeTvPlay.calls[0][1], URL.encode("utf-8"))
        self.assertEqual(ui_dialog_notification.shown, [])
        # Without an active Sim, the Sim that ran the interaction watches.
        self.assertIs(self.sim.pushed[0][1], self.tv)

    def test_the_active_sim_watches(self):
        active = support.FakeSim()
        support.services.active_sim = active
        dialog = self.show_dialog()
        dialog.respond(ui_dialog_generic.OK, URL)
        self.assertEqual(len(active.pushed), 1)
        self.assertEqual(self.sim.pushed, [])

    def test_ok_with_an_incorrect_url_shows_the_error(self):
        dialog = self.show_dialog()
        dialog.respond(ui_dialog_generic.OK, "https://vimeo.com/123")
        self.assertFalse(session.has_session(self.tv.id))
        self.assertEqual(self.dll.YoutubeTvPlay.calls, [])
        self.assertEqual(len(ui_dialog_notification.shown), 1)
        self.assertIn("vimeo.com", ui_dialog_notification.shown[0].kwargs["text"]().text)

    def test_ok_with_no_text_shows_the_error(self):
        dialog = self.show_dialog()
        dialog.respond(ui_dialog_generic.OK)
        self.assertEqual(len(ui_dialog_notification.shown), 1)

    def test_cancel_does_nothing(self):
        dialog = self.show_dialog()
        dialog.respond(ui_dialog_generic.CANCEL, URL)
        self.assertFalse(session.has_session(self.tv.id))
        self.assertEqual(self.dll.YoutubeTvPlay.calls, [])
        self.assertEqual(ui_dialog_notification.shown, [])

    def test_a_refusal_of_the_dll_shows_the_error(self):
        self.dll.YoutubeTvPlay.result = 0
        self.dll.error = b"the helper did not start"
        dialog = self.show_dialog()
        dialog.respond(ui_dialog_generic.OK, URL)
        self.assertIn("the helper did not start", ui_dialog_notification.shown[0].kwargs["text"]().text)

    def test_an_error_in_the_dialog_does_not_stop_the_interaction(self):
        original = ui_dialog_generic.UiDialogTextInputOkCancel.TunableFactory

        def fail(**kwargs):
            raise RuntimeError("no factory")
        ui_dialog_generic.UiDialogTextInputOkCancel.TunableFactory = fail
        try:
            self.assertTrue(run(Interaction(self.tv, self.sim)))
        finally:
            ui_dialog_generic.UiDialogTextInputOkCancel.TunableFactory = original


if __name__ == "__main__":
    unittest.main()
