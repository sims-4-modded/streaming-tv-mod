# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The log of the script mod: %LOCALAPPDATA%\\Sims4Mods\\logs\\youtube_tv_script.log.

The log is never in the game folder. A function of this module never raises an exception: if the mod cannot write the
log, the line is lost and the game continues.
"""

import os
import time
import traceback

LOG_NAME = "youtube_tv_script.log"

# When the log is larger than this size at the first write, the mod keeps it as youtube_tv_script.log.1.
_MAX_SIZE = 1024 * 1024

_path = None
_checked_size = False


def default_path():
    """Return the path of the log, or None if LOCALAPPDATA is not set."""
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        return None
    return os.path.join(local, "Sims4Mods", "logs", LOG_NAME)


def set_path(path):
    """Set the path of the log. The tests use this to write the log in a temporary folder."""
    global _path, _checked_size
    _path = path
    _checked_size = False


def path():
    """Return the path that the log uses now."""
    global _path
    if _path is None:
        _path = default_path()
    return _path


def _keep_old_log(file_path):
    global _checked_size
    _checked_size = True
    try:
        if os.path.getsize(file_path) > _MAX_SIZE:
            old = file_path + ".1"
            if os.path.exists(old):
                os.remove(old)
            os.rename(file_path, old)
    except OSError:
        pass


def write(message):
    """Write one line with the time to the log."""
    try:
        file_path = path()
        if file_path is None:
            return
        folder = os.path.dirname(file_path)
        if folder and not os.path.isdir(folder):
            os.makedirs(folder, exist_ok=True)
        if not _checked_size:
            _keep_old_log(file_path)
        line = "{} {}\n".format(time.strftime("%Y-%m-%d %H:%M:%S"), message)
        with open(file_path, "a", encoding="utf-8") as file:
            file.write(line)
    except Exception:  # noqa: BLE001 - the log must never stop the game.
        pass


def exception(message):
    """Write a line and the traceback of the exception that the code handles now."""
    try:
        write("{}\n{}".format(message, traceback.format_exc().rstrip()))
    except Exception:  # noqa: BLE001
        pass
