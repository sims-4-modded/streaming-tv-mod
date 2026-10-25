# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""Prevent the mute and the distortion of the client on a TV that shows a video of the mod, at the speeds 2 and 3.

The client mutes the video of a TV at the mute_speed of the playlist and higher. It distorts the screen at the
distortion_speed and higher. Some TVs have the speed 2 for both values. The DLL mod plays the video at the rate 2 with
its audio at the speeds 2 and 3, so the effect of the game must start only at the super speed.

The Video component of the game makes each new playlist from its own values mute_speed and distortion_speed. Each
object has its own component, so the mod changes these values only on the component of the TV of the session:

- At the start of a session, the mod sets both values to SUPER_SPEED3 and keeps the old values. The current
  playlist of the TV keeps its own copy of the values, so the mod also changes the playlist and tells the game to send
  it again (_resend_video_playlist).
- At the end of a session, the mod writes the old values back to the component and to the current playlist. The game
  sends the playlist again, except when the zone unloads or the game removes the TV.

The mod does not change the class of the component or other TVs.
"""

from . import log
from . import tv

# The value of clock.ClockSpeedMode.SUPER_SPEED3, if the mod cannot import the clock module.
SUPER_SPEED3 = 5

_FIELDS = ("mute_speed", "distortion_speed")

# The object id of the TV to (the Video component, the old values of _FIELDS).
_overrides = {}


def _super_speed():
    try:
        from clock import ClockSpeedMode
        return ClockSpeedMode.SUPER_SPEED3
    except Exception:  # noqa: BLE001
        return SUPER_SPEED3


def _write_playlist(owner, values, resend):
    """Write the values to the current playlist of the owner. Tell the game to send it again if a value changed."""
    playlist = getattr(owner, "video_playlist", None)
    if playlist is None:
        return False
    changed = False
    for name, value in values.items():
        if hasattr(playlist, name) and getattr(playlist, name) != value:
            setattr(playlist, name, value)
            changed = True
    if changed and resend:
        send = getattr(owner, "_resend_video_playlist", None)
        if send is not None:
            send()
    return changed


def is_overridden(object_id):
    return object_id in _overrides


def apply(obj):
    """Set the mute speed and the distortion speed of the TV to the super speed. Return True if the values changed."""
    try:
        object_id = getattr(obj, "id", None)
        component = tv.video_component(obj)
        if object_id is None or component is None:
            return False
        if object_id not in _overrides:
            old = {name: getattr(component, name) for name in _FIELDS if hasattr(component, name)}
            _overrides[object_id] = (component, old)
        component, old = _overrides[object_id]
        value = _super_speed()
        for name in old:
            setattr(component, name, value)
        _write_playlist(obj, {name: value for name in old}, resend=True)
        log.write("speed_override: the object {} mutes and distorts only at the super speed (old values: {})".format(
            object_id, ", ".join("{} {}".format(name, getattr(old[name], "name", old[name])) for name in old)))
        return True
    except Exception:  # noqa: BLE001
        log.exception("speed_override: the change of the object {} failed".format(getattr(obj, "id", None)))
        return False


def restore(object_id, obj=None, resend=True):
    """Write the old mute speed and distortion speed back to the TV. Return True if the mod changed the TV before.

    Set resend to False when the zone unloads or the game removes the TV: the mod then does not send the playlist.
    """
    entry = _overrides.pop(object_id, None)
    if entry is None:
        return False
    component, old = entry
    try:
        value = _super_speed()
        for name, old_value in old.items():
            if getattr(component, name, None) == value:
                setattr(component, name, old_value)
        owner = obj if obj is not None else getattr(component, "owner", None)
        playlist = getattr(owner, "video_playlist", None)
        if playlist is not None:
            # Change only the values that the mod set. A playlist with other values is not from the mod.
            ours = {name: old_value for name, old_value in old.items() if getattr(playlist, name, None) == value}
            _write_playlist(owner, ours, resend=resend)
        log.write("speed_override: the object {} has its old mute speed and distortion speed".format(object_id))
    except Exception:  # noqa: BLE001
        log.exception("speed_override: the restore of the object {} failed".format(object_id))
    return True


def restore_all(resend=False):
    """Write the old values back to each TV that the mod changed."""
    for object_id in list(_overrides):
        restore(object_id, resend=resend)


def clear():
    """Forget the changed TVs without a change of the game. The tests use this function."""
    _overrides.clear()
