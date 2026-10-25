# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The videos that play now: one session for each TV, found by the object id of the TV.

A session starts when the player gives a URL. The mod pushes the watch interaction of the game on the Sim (watch.py),
tells the DLL mod the URL and the transform of the TV, and silences the sound of the game channel on that TV
(channel_audio.py). The watch interaction puts the TV on its channel. The game then sends the playlist of the channel,
and the DLL mod links the channel of the TV to its object id and shows the frames of the video in place of the frames
of the channel.

A session stops when:

- the watch interaction of the session ends (the Sim stops watching, the player cancels it, the player picks another
  channel, or the TV turns off: each of these ends the interaction),
- the game removes the Video component (the player sells or removes the TV),
- the zone unloads,
- the player uses the cheat command youtube_tv.stop (the mod then cancels the watch interaction).

While the watch interaction runs, a playlist that the game sets on the TV does not stop the session: the channel of
the watch interaction sets it. A playlist on a TV whose session has no running watch interaction stops the session.

The wraps of this module (each wrap calls the original game function):

- objects.components.video.VideoComponent.set_video_clips and add_video_clips: the original first, then the check
  above.
- objects.components.video.VideoComponent.on_remove: the session stops, then the original.
- zone.Zone.on_teardown: all sessions stop, then the original. The objects of the zone still exist at that time.

The mod sends the transform when the TV moves, through the location callback of the object
(register_on_location_changed).
"""

import functools

from . import channel_audio
from . import log
from . import native
from . import tv
from . import watch

_WRAP_MARK = "_ts4_youtube_tv_wrapped"


class Session:
    """One video on one TV, while one Sim watches it."""

    def __init__(self, object_id, url, interaction=None):
        self.object_id = object_id
        self.url = url
        self.interaction = interaction
        self.finishing_callback = None
        self.location_callback = None

    def watching(self):
        """Return True while the watch interaction of the session runs."""
        if self.interaction is None:
            return False
        return not getattr(self.interaction, "is_finishing", False)


_sessions = {}


def sessions():
    """Return a copy of the sessions: object id to Session."""
    return dict(_sessions)


def has_session(object_id):
    return object_id in _sessions


def _make_location_callback(object_id):
    def on_location_changed(obj, *args, **kwargs):
        try:
            if object_id in _sessions:
                position, orientation = tv.transform(obj)
                native.set_transform(object_id, position, orientation)
        except Exception:  # noqa: BLE001
            log.exception("session: the transform of the object {} failed".format(object_id))
    return on_location_changed


def _make_finishing_callback(object_id):
    def on_finishing(interaction):
        try:
            session = _sessions.get(object_id)
            if session is not None and session.interaction is interaction:
                _stop_session(object_id, "the watch interaction ended", cancel=False)
        except Exception:  # noqa: BLE001
            log.exception("session: the end of the watch interaction of the object {} failed".format(object_id))
    return on_finishing


def start(obj, url, sim=None):
    """Make the Sim watch the TV and start the video of the URL on it.

    Return (True, None) or (False, the error for the player).
    """
    try:
        if not native.available():
            return False, "the DLL mod youtube_tv.dll is not ready: {}".format(native.reason())
        if tv.video_component(obj) is None:
            return False, "this object has no video screen"
        object_id = obj.id
        if object_id in _sessions:
            _stop_session(object_id, "a new video", cancel=False, obj=obj)
        interaction, error = watch.push_watch(sim, obj)
        if interaction is None:
            return False, error
        position, orientation = tv.transform(obj)
        ok, error = native.play(object_id, url, position, orientation)
        if not ok:
            _cancel(interaction, "the DLL mod refused the video")
            return False, error
        session = Session(object_id, url, interaction)
        _sessions[object_id] = session
        session.finishing_callback = _make_finishing_callback(object_id)
        interaction.register_on_finishing_callback(session.finishing_callback)
        channel_audio.silence(obj)
        register = getattr(obj, "register_on_location_changed", None)
        if register is not None:
            session.location_callback = _make_location_callback(object_id)
            register(session.location_callback)
        log.write("session: started on the object {}: {}".format(object_id, url))
        return True, None
    except Exception:  # noqa: BLE001
        log.exception("session: the start of the video failed")
        return False, "the mod could not start the video (see youtube_tv_script.log)"


def _cancel(interaction, reason):
    """Cancel the watch interaction, if it still runs."""
    try:
        if interaction is None or getattr(interaction, "is_finishing", False):
            return
        from interactions.interaction_finisher import FinishingType
        interaction.cancel(FinishingType.USER_CANCEL, cancel_reason_msg="YouTube TV: {}".format(reason))
    except Exception:  # noqa: BLE001
        log.exception("session: the cancel of the watch interaction failed")


def _stop_session(object_id, reason, cancel, obj=None):
    session = _sessions.pop(object_id, None)
    if session is None:
        return False
    native.stop(object_id)
    try:
        if obj is None:
            import services
            manager = services.object_manager()
            obj = manager.get(object_id) if manager is not None else None
        channel_audio.release(obj, object_id)
        if session.interaction is not None and session.finishing_callback is not None:
            unregister = getattr(session.interaction, "unregister_on_finishing_callback", None)
            if unregister is not None:
                unregister(session.finishing_callback)
        if cancel:
            _cancel(session.interaction, reason)
        if obj is not None and session.location_callback is not None:
            unregister = getattr(obj, "unregister_on_location_changed", None)
            if unregister is not None:
                unregister(session.location_callback)
    except Exception:  # noqa: BLE001
        log.exception("session: the clean-up of the object {} failed".format(object_id))
    log.write("session: stopped on the object {}: {}".format(object_id, reason))
    return True


def stop(object_id, reason="the player stopped it"):
    """Stop the session of one TV and cancel its watch interaction. Return True if a session stopped."""
    try:
        return _stop_session(object_id, reason, cancel=True)
    except Exception:  # noqa: BLE001
        log.exception("session: the stop of the object {} failed".format(object_id))
        return False


def stop_all(reason, cancel=False):
    """Stop every session. The DLL mod also stops every video."""
    try:
        for object_id in list(_sessions):
            _stop_session(object_id, reason, cancel=cancel)
        native.stop_all()
    except Exception:  # noqa: BLE001
        log.exception("session: the stop of all sessions failed")


def on_foreign_playlist(component, call_name):
    """Stop the session of the owner of the component, unless its watch interaction runs.

    The channel of the watch interaction sets the playlist of the TV, so a playlist does not stop a session that
    watches.
    """
    owner = getattr(component, "owner", None)
    object_id = getattr(owner, "id", None)
    session = _sessions.get(object_id)
    if session is None or session.watching():
        return
    _stop_session(object_id, "the game called {}".format(call_name), cancel=False, obj=owner)


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
                _stop_session(object_id, "the game removed the Video component", cancel=False, obj=owner)
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
    """Wrap the functions of the game that stop a session, and the channel sound. A second call does nothing."""
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
    channel_audio.install()


def clear():
    """Forget every session without a call to the game. The tests use this function."""
    _sessions.clear()
    channel_audio.clear()
