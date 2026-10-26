# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The shared parts of the tests of the script mod.

Import this module before the mod. It puts the fake game modules and the sources of the mod on sys.path, and it sets
LOCALAPPDATA to a temporary folder, so that the log and the settings of the tests never go to the folders of the user.
"""

import os
import sys
import tempfile

# The tests do not write bytecode into the source folders.
sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
FAKES = os.path.join(HERE, "fakes")
PYTHON = os.path.join(ROOT, "python")
TOOLS = os.path.join(ROOT, "tools")

for _path in (FAKES, PYTHON, TOOLS):
    if _path not in sys.path:
        sys.path.insert(0, _path)

TEMP = tempfile.mkdtemp(prefix="ts4_youtube_tv_tests_")
os.environ["LOCALAPPDATA"] = TEMP

import services  # noqa: E402 - the fake module, after the change of sys.path.
import tag  # noqa: E402
from objects.components import types  # noqa: E402
from objects.components.state import StateComponentManagedDistributables  # noqa: E402
from objects.components.video import VideoComponent  # noqa: E402
from objects.electronics.television import WatchSuperInteraction  # noqa: E402

import ts4_youtube_tv  # noqa: E402,F401 - the import starts the mod with the fakes.
from ts4_youtube_tv import native  # noqa: E402
from ts4_youtube_tv import session  # noqa: E402
from ts4_youtube_tv import settings  # noqa: E402


class Vector:
    def __init__(self, x, y, z, w=None):
        self.x, self.y, self.z, self.w = x, y, z, w


class tv_WatchNews(WatchSuperInteraction):  # noqa: N801 - the name of the tuning of the game.
    pass


class tv_WatchAction(WatchSuperInteraction):  # noqa: N801
    pass


class tv_WatchFireplace(WatchSuperInteraction):  # noqa: N801
    pass


class tv_ChannelSurf:  # noqa: N801 - not a watch interaction.
    pass


class FakeSound:
    """A started client state change of the sound of a channel."""

    def __init__(self, target):
        self.target = target
        self.playing = False
        self.starts = 0
        self.stops = 0

    def start(self):
        self.playing = True
        self.starts += 1

    def stop(self, immediate=False):
        self.playing = False
        self.stops += 1


class FakeState:
    overridden_by = None


class FakeStateComponent:
    def __init__(self, usable=True):
        self.usable = usable
        self._custom_distributables = StateComponentManagedDistributables()

    def is_object_usable(self):
        return self.usable


class FakeObject:
    """A fake game object with tags, components, a transform and location callbacks."""

    _next_id = 100

    def __init__(self, tags=(tag.Tag.Func_TV,), video=True, usable=True, position=(1.0, 2.0, 3.0),
                 affordances=(tv_ChannelSurf, tv_WatchAction, tv_WatchNews)):
        FakeObject._next_id += 1
        self.id = FakeObject._next_id
        self.is_sim = False
        self._tags = set(tags)
        self._video_playlist = None
        # The messages of the playlist that the fake distributor sent to the client.
        self.sent_playlists = []
        self.position = Vector(*position)
        self.orientation = Vector(0.0, 0.5, 0.0, 0.5)
        self.forward = Vector(0.0, 0.0, 1.0)
        self.state_component = FakeStateComponent(usable)
        self.location_callbacks = []
        self._components = {types.VIDEO_COMPONENT: VideoComponent(self)} if video else {}
        self.affordances = list(affordances)

    @property
    def video_playlist(self):
        return self._video_playlist

    @video_playlist.setter
    def video_playlist(self, playlist):
        """Keep the playlist and send its message, as the distributor field of the game does."""
        self._video_playlist = playlist
        if playlist is not None:
            self.sent_playlists.append(playlist.get_protocol_msg())

    def _resend_video_playlist(self):
        if self._video_playlist is not None:
            self.sent_playlists.append(self._video_playlist.get_protocol_msg())

    def super_affordances(self, context=None):
        return iter(self.affordances)

    def set_channel_sound(self, state=None):
        """Apply the sound of a channel through the managed distributables, as the game does."""
        managed = self.state_component._custom_distributables
        managed.apply(self, "audio_state", state or FakeState, None, FakeSound)
        return managed._distributables_map[("audio_state", state or FakeState)]

    def has_tag(self, value):
        return value in self._tags

    def get_component(self, definition):
        return self._components.get(definition)

    def register_on_location_changed(self, callback):
        self.location_callbacks.append(callback)

    def unregister_on_location_changed(self, callback):
        self.location_callbacks.remove(callback)

    def move(self, x, y, z):
        self.position = Vector(x, y, z)
        for callback in list(self.location_callbacks):
            callback(self, None, None)


class FakeInteraction:
    """A running interaction with finishing callbacks."""

    def __init__(self, affordance, target):
        self.affordance = affordance
        self.target = target
        self.is_finishing = False
        self.callbacks = []
        self.cancels = []

    def register_on_finishing_callback(self, callback):
        self.callbacks.append(callback)

    def unregister_on_finishing_callback(self, callback):
        if callback in self.callbacks:
            self.callbacks.remove(callback)

    def finish(self):
        self.is_finishing = True
        for callback in list(self.callbacks):
            callback(self)

    def cancel(self, finishing_type, cancel_reason_msg=None):
        self.cancels.append((finishing_type, cancel_reason_msg))
        self.finish()


class FakeEnqueueResult:
    def __init__(self, interaction):
        self.interaction = interaction

    def __bool__(self):
        return self.interaction is not None


class FakeSim:
    """A Sim that records the pushed interactions."""

    _next_id = 900

    def __init__(self, accepts=True, position=(0.0, 0.0, 0.0)):
        FakeSim._next_id += 1
        self.id = FakeSim._next_id
        self.is_sim = True
        self.position = Vector(*position)
        self.accepts = accepts
        self.pushed = []
        self.interactions = []

    def push_super_affordance(self, affordance, target, context, **kwargs):
        self.pushed.append((affordance, target, context))
        if not self.accepts:
            return FakeEnqueueResult(None)
        interaction = FakeInteraction(affordance, target)
        self.interactions.append(interaction)
        return FakeEnqueueResult(interaction)


class FakeObjectManager:
    def __init__(self, objects=()):
        self._objects = {obj.id: obj for obj in objects}

    def valid_objects(self):
        return list(self._objects.values())

    def get(self, object_id):
        return self._objects.get(object_id)


class FakeFunction:
    """One function of the fake DLL: it records each call and returns a set value."""

    def __init__(self, result=None, side_effect=None):
        self.result = result
        self.side_effect = side_effect
        self.calls = []
        self.argtypes = None
        self.restype = None

    def __call__(self, *args):
        self.calls.append(args)
        if self.side_effect is not None:
            return self.side_effect(*args)
        return self.result


class FakeDll:
    """A fake youtube_tv.dll with the functions of the bridge. Only version 2 or more has YoutubeTvSetPlaylistClips."""

    def __init__(self, version=1, play_result=1, error=b""):
        self.error = error
        self.YoutubeTvApiVersion = FakeFunction(version)
        self.YoutubeTvPlay = FakeFunction(play_result)
        self.YoutubeTvStop = FakeFunction(1)
        self.YoutubeTvStopAll = FakeFunction(None)
        self.YoutubeTvSetSpeed = FakeFunction(None)
        self.YoutubeTvSetTransform = FakeFunction(None)
        self.YoutubeTvSetScreenKey = FakeFunction(None)
        self.YoutubeTvLastError = FakeFunction(side_effect=self._last_error)
        self.set_frame_mode = FakeFunction(1)
        if version >= 2:
            self.YoutubeTvSetPlaylistClips = FakeFunction(1)

    def _last_error(self, buffer, size):
        data = self.error[:size - 1]
        buffer.value = data
        return len(data)


def use_dll(dll):
    """Make the bridge use the fake DLL (or no DLL for None)."""
    native.reset(opener=lambda: (dll, None) if dll is not None else (None, "youtube_tv.dll is not loaded"))
    return dll


def reset_game(objects=(), active_sim=None):
    """Reset the fake game: the sessions, the settings, the object manager and the active Sim."""
    session.clear()
    settings.set_values({})
    services.reset()
    services.objects = FakeObjectManager(objects)
    services.active_sim = active_sim
