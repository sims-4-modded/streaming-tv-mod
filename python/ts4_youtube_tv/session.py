# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The videos that play now: one session for each TV, found by the object id of the TV.

A session starts when the player gives a URL. The mod puts the screen clip on the TV with set_video_clips, and tells
the DLL mod the clip key, the URL and the transform of the TV. A session stops when:

- the game sets a different playlist on that TV (the TV goes off, the channel changes, the TV breaks),
- the game removes the Video component (the player sells or removes the TV),
- the zone unloads,
- the player uses the cheat command youtube_tv.stop.

The wraps of this module (each wrap calls the original game function):

- objects.components.video.VideoComponent.set_video_clips and add_video_clips: the original first, then the session
  of that TV stops, if the call did not come from this module.
- objects.components.video.VideoComponent.on_remove: the session stops, then the original.
- zone.Zone.on_teardown: all sessions stop, then the original. The objects of the zone still exist at that time.

The mod sends the transform when the TV moves, through the location callback of the object
(register_on_location_changed).
"""

import functools

from . import log
from . import native
from . import tv

_WRAP_MARK = "_ts4_youtube_tv_wrapped"


class Session:
    """One video on one TV."""

    def __init__(self, object_id, url, clip_name, key):
        self.object_id = object_id
        self.url = url
        self.clip_name = clip_name
        self.key = key
        self.location_callback = None


_sessions = {}
# The object ids of the TVs for which this module calls set_video_clips now.
_own_calls = set()


def sessions():
    """Return a copy of the sessions: object id to Session."""
    return dict(_sessions)


def has_session(object_id):
    return object_id in _sessions


def _set_clips(component, object_id, clip_names, loop_last):
    """Call set_video_clips on the component, and mark the call as our own call."""
    _own_calls.add(object_id)
    try:
        component.set_video_clips(clip_names, loop_last=loop_last)
    finally:
        _own_calls.discard(object_id)


def _make_location_callback(object_id):
    def on_location_changed(obj, *args, **kwargs):
        try:
            if object_id in _sessions:
                position, orientation = tv.transform(obj)
                native.set_transform(object_id, position, orientation)
        except Exception:  # noqa: BLE001
            log.exception("session: the transform of the object {} failed".format(object_id))
    return on_location_changed


def start(obj, url):
    """Start the video of the URL on the TV. Return (True, None) or (False, the error for the player)."""
    try:
        if not native.available():
            return False, "the DLL mod youtube_tv.dll is not ready: {}".format(native.reason())
        component = tv.video_component(obj)
        if component is None:
            return False, "this object has no video screen"
        object_id = obj.id
        if object_id in _sessions:
            _stop_session(object_id, "a new video", clear_screen=False, obj=obj)
        clip_name = tv.screen_clip_name()
        key = tv.clip_key(clip_name)
        _set_clips(component, object_id, [clip_name], loop_last=True)
        native.set_screen_key(object_id, *key)
        position, orientation = tv.transform(obj)
        ok, error = native.play(object_id, url, position, orientation)
        if not ok:
            _set_clips(component, object_id, [], loop_last=False)
            return False, error
        session = Session(object_id, url, clip_name, key)
        _sessions[object_id] = session
        register = getattr(obj, "register_on_location_changed", None)
        if register is not None:
            session.location_callback = _make_location_callback(object_id)
            register(session.location_callback)
        log.write("session: started on the object {}: {}".format(object_id, url))
        return True, None
    except Exception:  # noqa: BLE001
        log.exception("session: the start of the video failed")
        return False, "the mod could not start the video (see youtube_tv_script.log)"


def _stop_session(object_id, reason, clear_screen, obj=None):
    session = _sessions.pop(object_id, None)
    if session is None:
        return False
    native.stop(object_id)
    try:
        if obj is None:
            import services
            manager = services.object_manager()
            obj = manager.get(object_id) if manager is not None else None
        if obj is not None and session.location_callback is not None:
            unregister = getattr(obj, "unregister_on_location_changed", None)
            if unregister is not None:
                unregister(session.location_callback)
        if clear_screen and obj is not None:
            component = tv.video_component(obj)
            if component is not None:
                _set_clips(component, object_id, [], loop_last=False)
    except Exception:  # noqa: BLE001
        log.exception("session: the clean-up of the object {} failed".format(object_id))
    log.write("session: stopped on the object {}: {}".format(object_id, reason))
    return True


def stop(object_id, reason="the player stopped it"):
    """Stop the session of one TV and clear its screen. Return True if a session stopped."""
    try:
        return _stop_session(object_id, reason, clear_screen=True)
    except Exception:  # noqa: BLE001
        log.exception("session: the stop of the object {} failed".format(object_id))
        return False


def stop_all(reason, clear_screens=False):
    """Stop every session. The DLL mod also stops every video."""
    try:
        for object_id in list(_sessions):
            _stop_session(object_id, reason, clear_screen=clear_screens)
        native.stop_all()
    except Exception:  # noqa: BLE001
        log.exception("session: the stop of all sessions failed")


def on_foreign_playlist(component, call_name):
    """Stop the session of the owner of the component, if the call did not come from this module."""
    owner = getattr(component, "owner", None)
    object_id = getattr(owner, "id", None)
    if object_id is None or object_id in _own_calls or object_id not in _sessions:
        return
    _stop_session(object_id, "the game called {}".format(call_name), clear_screen=False, obj=owner)


def _wrap_after(original, call_name):
    """Wrap a method of the Video component: call the original first, then check the session."""
    @functools.wraps(original)
    def wrapper(self, *args, **kwargs):
        result = original(self, *args, **kwargs)
        try:
            on_foreign_playlist(self, call_name)
        except Exception:  # noqa: BLE001
            log.exception("session: the wrap of {} failed".format(call_name))
        return result
    setattr(wrapper, _WRAP_MARK, True)
    return wrapper


def _wrap_on_remove(original):
    @functools.wraps(original)
    def wrapper(self, *args, **kwargs):
        try:
            owner = getattr(self, "owner", None)
            object_id = getattr(owner, "id", None)
            if object_id in _sessions:
                _stop_session(object_id, "the game removed the Video component", clear_screen=False, obj=owner)
        except Exception:  # noqa: BLE001
            log.exception("session: the wrap of on_remove failed")
        return original(self, *args, **kwargs)
    setattr(wrapper, _WRAP_MARK, True)
    return wrapper


def _wrap_on_teardown(original):
    @functools.wraps(original)
    def wrapper(self, *args, **kwargs):
        try:
            stop_all("the zone unloads")
        except Exception:  # noqa: BLE001
            log.exception("session: the wrap of Zone.on_teardown failed")
        return original(self, *args, **kwargs)
    setattr(wrapper, _WRAP_MARK, True)
    return wrapper


def _install_wrap(owner, name, make_wrapper):
    original = getattr(owner, name, None)
    if original is None:
        log.write("session: {}.{} does not exist; the mod does not wrap it".format(owner.__name__, name))
        return False
    if getattr(original, _WRAP_MARK, False):
        return True
    setattr(owner, name, make_wrapper(original))
    return True


def install():
    """Wrap the functions of the game that stop a session. A second call does nothing."""
    try:
        from objects.components.video import VideoComponent
        _install_wrap(VideoComponent, "set_video_clips", lambda f: _wrap_after(f, "set_video_clips"))
        _install_wrap(VideoComponent, "add_video_clips", lambda f: _wrap_after(f, "add_video_clips"))
        _install_wrap(VideoComponent, "on_remove", _wrap_on_remove)
    except Exception:  # noqa: BLE001
        log.exception("session: the wraps of VideoComponent failed")
    try:
        import zone
        _install_wrap(zone.Zone, "on_teardown", _wrap_on_teardown)
    except Exception:  # noqa: BLE001
        log.exception("session: the wrap of Zone.on_teardown failed")


def clear():
    """Forget every session without a call to the game. The tests use this function."""
    _sessions.clear()
    _own_calls.clear()
