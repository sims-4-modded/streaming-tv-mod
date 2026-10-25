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
from objects.components.video import VideoComponent  # noqa: E402

import ts4_youtube_tv  # noqa: E402,F401 - the import starts the mod with the fakes.
from ts4_youtube_tv import native  # noqa: E402
from ts4_youtube_tv import session  # noqa: E402
from ts4_youtube_tv import settings  # noqa: E402


class Vector:
    def __init__(self, x, y, z, w=None):
        self.x, self.y, self.z, self.w = x, y, z, w


class FakeStateComponent:
    def __init__(self, usable=True):
        self.usable = usable

    def is_object_usable(self):
        return self.usable


class FakeObject:
    """A fake game object with tags, components, a transform and location callbacks."""

    _next_id = 100

    def __init__(self, tags=(tag.Tag.Func_TV,), video=True, usable=True, position=(1.0, 2.0, 3.0)):
        FakeObject._next_id += 1
        self.id = FakeObject._next_id
        self.is_sim = False
        self._tags = set(tags)
        self.video_playlist = None
        self.position = Vector(*position)
        self.orientation = Vector(0.0, 0.5, 0.0, 0.5)
        self.state_component = FakeStateComponent(usable)
        self.location_callbacks = []
        self._components = {types.VIDEO_COMPONENT: VideoComponent(self)} if video else {}

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
    """A fake youtube_tv.dll with the functions of bridge version 1."""

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
