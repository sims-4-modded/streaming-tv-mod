# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The interaction "Watch a YouTube video" on the pie menu of each TV.

The tuning of the interaction (tuning/ts4_youtube_tv_WatchYoutubeVideo.xml, in the package of the mod) names the
class WatchYoutubeVideoInteraction of this module. The interaction is an immediate super interaction: the Sim does not
walk to the TV. The interaction shows the URL dialog. On OK with a correct URL, the session of the TV starts. On
Cancel, nothing occurs. For an incorrect URL, a notification shows the error.
"""

from event_testing.results import TestResult
from interactions.base.immediate_interaction import ImmediateSuperInteraction
from sims4.utils import flexmethod
from singletons import DEFAULT

from . import dialogs
from . import fnv
from . import log
from . import native
from . import session
from . import settings
from . import tv
from . import urls

TUNING_NAME = "ts4_youtube_tv:WatchYoutubeVideo"
INSTANCE_ID = fnv.fnv1_64(TUNING_NAME)
DISPLAY_NAME = "Watch a YouTube video"


def on_url(target, owner, text):
    """Check the URL from the dialog and start the video, or show the error."""
    try:
        url = (text or "").strip()
        try:
            urls.parse_youtube_video(url)
        except urls.UrlError as error:
            log.write("interaction: the URL is not correct: {}".format(error))
            dialogs.show_error(owner, "This is not the URL of a YouTube video: {}".format(error))
            return False
        ok, error = session.start(target, url)
        if not ok:
            dialogs.show_error(owner, "The video did not start: {}".format(error))
        return ok
    except Exception:  # noqa: BLE001
        log.exception("interaction: the URL handling failed")
        return False


class WatchYoutubeVideoInteraction(ImmediateSuperInteraction):
    """Show the URL dialog, then start the video on the target TV."""

    @classmethod
    def _test(cls, target, context, **interaction_parameters):
        try:
            if not settings.get("enabled"):
                return TestResult(False, "The YouTube TV is off in the settings.")
            if not tv.is_usable_tv(target):
                return TestResult(False, "The target is not a TV that works.")
            if not native.available():
                return TestResult(False, "The DLL mod youtube_tv.dll is not ready.")
        except Exception:  # noqa: BLE001
            log.exception("interaction: the test failed")
            return TestResult(False, "The test of the YouTube TV failed.")
        return super()._test(target, context, **interaction_parameters)

    @flexmethod
    def _get_name(cls, inst, target=DEFAULT, context=DEFAULT, **interaction_parameters):
        # Raw text shows the same name in each language of the game.
        from sims4.localization import LocalizationHelperTuning
        return LocalizationHelperTuning.get_raw_text(DISPLAY_NAME)

    def _run_interaction_gen(self, timeline):
        try:
            target = self.target
            owner = self.sim
            dialogs.show_url_dialog(owner, lambda text: on_url(target, owner, text),
                                    resolver=self.get_resolver())
        except Exception:  # noqa: BLE001
            log.exception("interaction: the URL dialog failed")
        return True
        # The game runs this function as a generator, so the function must have a yield.
        yield
