# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The cheat commands of the mod. Each command catches each exception and writes the error to the console.

- youtube_tv.play <url>: start the video on the TV that is nearest to the active Sim.
- youtube_tv.stop: stop every video of the mod and clear the screens.
- youtube_tv.status: show the bridge, the settings, the sessions and the last speed.
- youtube_tv.info: show the values of the Video component of the nearest TV (mute_speed, distortion_speed, the
  playlist).
"""

import sims4.commands

from . import clock
from . import log
from . import native
from . import session
from . import settings
from . import tv
from . import urls


def _output(text, connection):
    sims4.commands.output(text, connection)
    log.write("command: {}".format(text))


def _active_sim():
    import services
    return services.get_active_sim()


def run_play(url_parts, connection):
    """Start the video of the URL on the TV that is nearest to the active Sim."""
    url = "".join(url_parts).strip()
    if not url:
        _output("Usage: youtube_tv.play <url>", connection)
        return False
    if not settings.get("enabled"):
        _output("The YouTube TV is off in the settings.", connection)
        return False
    try:
        urls.parse_youtube_video(url)
    except urls.UrlError as error:
        _output("This is not the URL of a YouTube video: {}".format(error), connection)
        return False
    target = tv.nearest_tv(_active_sim())
    if target is None:
        _output("There is no TV that works on this lot.", connection)
        return False
    ok, error = session.start(target, url, _active_sim())
    if ok:
        _output("The active Sim watches the video on the TV {} ({}).".format(target.id, type(target).__name__),
                connection)
    else:
        _output("The video did not start: {}".format(error), connection)
    return ok


def run_stop(connection):
    """Stop every video of the mod."""
    count = len(session.sessions())
    for object_id in list(session.sessions()):
        session.stop(object_id)
    native.stop_all()
    _output("Stopped {} video(s).".format(count), connection)
    return count


def run_status(connection):
    """Show the state of the mod."""
    if native.available():
        _output("Bridge: ready, API version {}.".format(native.api_version()), connection)
    else:
        _output("Bridge: off: {}".format(native.reason()), connection)
    _output("Settings: enabled={}, watch_affordances={}.".format(
        settings.get("enabled"), ", ".join(settings.get("watch_affordances"))), connection)
    sessions = session.sessions()
    _output("Sessions: {}.".format(len(sessions)), connection)
    for object_id, item in sessions.items():
        _output("  TV {}: {}".format(object_id, item.url), connection)
    _output("Last speed sent: {}.".format(
        "none" if clock.last_sent is None else "mode={}, scale={}, super_speed={}".format(*clock.last_sent)),
        connection)
    dll_status = native.status_text()
    if dll_status:
        _output("DLL: {}".format(dll_status), connection)
    return True


def _key_text(key):
    """Return type:group:instance of a resource key (a protobuf ResourceKey or a key of sims4.resources)."""
    try:
        return "{:08X}:{:08X}:{:016X}".format(int(key.type), int(key.group), int(key.instance))
    except Exception:  # noqa: BLE001
        return repr(key)


def run_info(connection):
    """Show the values of the Video component of the nearest TV."""
    target = tv.nearest_tv(_active_sim())
    if target is None:
        _output("There is no TV that works on this lot.", connection)
        return False
    component = tv.video_component(target)
    _output("TV {} ({}).".format(target.id, type(target).__name__), connection)
    for name in ("video_display_type", "mute_speed", "distortion_speed", "speed_audio_clip_replacement"):
        _output("  {} = {!r}".format(name, getattr(component, name, None)), connection)
    playlist = getattr(target, "video_playlist", None)
    _output("  video_playlist = {}".format("None" if playlist is None else str(playlist)), connection)
    for key in getattr(playlist, "clip_keys", None) or ():
        _output("    clip {}".format(_key_text(key)), connection)
    _output("  session = {}".format(session.has_session(target.id)), connection)
    return True


def _safe(function, connection, *args):
    try:
        return function(*args, connection)
    except Exception:  # noqa: BLE001 - a cheat command must not raise into the game.
        log.exception("command: {} failed".format(function.__name__))
        try:
            sims4.commands.output("The command failed (see youtube_tv_script.log).", connection)
        except Exception:  # noqa: BLE001
            pass
        return False


@sims4.commands.Command("youtube_tv.play", command_type=sims4.commands.CommandType.Live)
def youtube_tv_play(*url_parts, _connection=None):
    return _safe(run_play, _connection, url_parts)


@sims4.commands.Command("youtube_tv.stop", command_type=sims4.commands.CommandType.Live)
def youtube_tv_stop(_connection=None):
    return _safe(run_stop, _connection)


@sims4.commands.Command("youtube_tv.status", command_type=sims4.commands.CommandType.Live)
def youtube_tv_status(_connection=None):
    return _safe(run_status, _connection)


@sims4.commands.Command("youtube_tv.info", command_type=sims4.commands.CommandType.Live)
def youtube_tv_info(_connection=None):
    return _safe(run_info, _connection)
