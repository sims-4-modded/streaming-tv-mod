# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""Silence the sound of the game channel on a TV that shows a video of the mod.

A channel of a TV is a state value of the object. The state value can start a sound on the object (the client state
change audio_state). The state component keeps each started change in its managed distributables, with the key
(attribute name, state). The video of the mod has its own 3D audio, so the sound of the channel must not play.

The wrap (the original first):

- objects.components.state.StateComponentManagedDistributables.apply: after the original, if the change is
  audio_state and the target is a TV that shows a video of the mod, the mod stops the new sound. The sound stays in
  the map of the game, so the game stops or replaces it as usual.

When the video stops, the mod starts the sounds of the TV again, if the TV still has them.
"""

import functools

from . import log

AUDIO_STATE = "audio_state"
_WRAP_MARK = "_ts4_youtube_tv_wrapped"

# The object ids of the TVs whose channel sound must not play.
_silenced = set()


def _distributables(obj):
    """Return the map of the managed distributables of the object, or an empty dict."""
    state_component = getattr(obj, "state_component", None)
    managed = getattr(state_component, "_custom_distributables", None)
    return getattr(managed, "_distributables_map", None) or {}


def _audio_items(distributables):
    return [item for key, item in list(distributables.items()) if isinstance(key, tuple) and key[:1] == (AUDIO_STATE,)]


def silence(obj):
    """Stop the channel sounds of the TV now and after each new channel. Return the count of stopped sounds."""
    object_id = getattr(obj, "id", None)
    if object_id is None:
        return 0
    _silenced.add(object_id)
    count = 0
    for item in _audio_items(_distributables(obj)):
        try:
            item.stop(immediate=True)
            count += 1
        except Exception:  # noqa: BLE001
            log.exception("channel_audio: the stop of a sound of the object {} failed".format(object_id))
    return count


def release(obj, object_id=None):
    """Let the channel sounds of the TV play again, and start the sounds that the TV still has."""
    if object_id is None:
        object_id = getattr(obj, "id", None)
    _silenced.discard(object_id)
    if obj is None:
        return 0
    count = 0
    for item in _audio_items(_distributables(obj)):
        try:
            item.start()
            count += 1
        except Exception:  # noqa: BLE001
            log.exception("channel_audio: the start of a sound of the object {} failed".format(object_id))
    return count


def is_silenced(object_id):
    return object_id in _silenced


def _wrap_apply(original):
    @functools.wraps(original)
    def wrapper(self, target, attr_name, state, *args, **kwargs):
        result = original(self, target, attr_name, state, *args, **kwargs)
        try:
            if attr_name == AUDIO_STATE and getattr(target, "id", None) in _silenced:
                state_owner = getattr(state, "overridden_by", None) or state
                item = getattr(self, "_distributables_map", {}).get((attr_name, state_owner))
                if item is not None:
                    item.stop(immediate=True)
                    log.write("channel_audio: stopped the channel sound of the object {}".format(target.id))
        except Exception:  # noqa: BLE001
            log.exception("channel_audio: the wrap of apply failed")
        return result
    setattr(wrapper, _WRAP_MARK, True)
    return wrapper


def install():
    """Wrap the managed distributables of the state component. A second call does nothing."""
    try:
        from objects.components.state import StateComponentManagedDistributables
        original = getattr(StateComponentManagedDistributables, "apply", None)
        if original is None:
            log.write("channel_audio: StateComponentManagedDistributables.apply does not exist")
            return False
        if not getattr(original, _WRAP_MARK, False):
            StateComponentManagedDistributables.apply = _wrap_apply(original)
        return True
    except Exception:  # noqa: BLE001
        log.exception("channel_audio: the wrap failed")
        return False


def clear():
    """Forget the silenced TVs. The tests use this function."""
    _silenced.clear()
