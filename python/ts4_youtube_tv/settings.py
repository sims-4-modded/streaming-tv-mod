# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The settings of the script mod: %LOCALAPPDATA%\\Sims4Mods\\youtube_tv_script.json.

The file is optional. A missing file, a missing field or a field with an incorrect value keeps the default, and the
mod writes one log line for each incorrect field.

The fields:

- watch_affordances: the names of the watch interactions of the game, in the order that the mod tries them when the
  Sim starts to watch a video (watch.py). If no name matches, the mod takes the first watch interaction of the TV.
- enabled: set it to false to hide the interaction and stop the cheat command play.
"""

import json
import os

from . import log

FILE_NAME = "youtube_tv_script.json"

# Watch interactions of the base game TVs. The news channel gives no strong buff, so it is the first choice.
DEFAULT_WATCH_AFFORDANCES = ("tv_WatchNews", "tv_WatchAction", "tv_WatchMusic")
MAX_WATCH_AFFORDANCES = 16

DEFAULTS = {
    "watch_affordances": DEFAULT_WATCH_AFFORDANCES,
    "enabled": True,
}

_NAME_CHARACTERS = frozenset("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")

_values = dict(DEFAULTS)


def default_path():
    """Return the path of the settings file, or None if LOCALAPPDATA is not set."""
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        return None
    return os.path.join(local, "Sims4Mods", FILE_NAME)


def is_tuning_name(text):
    """Return True if the text is a correct tuning name: 1 to 128 letters, digits, _ and -."""
    return isinstance(text, str) and 0 < len(text) <= 128 and all(c in _NAME_CHARACTERS for c in text)


def _parse_names(value):
    """Return the tuple of names, or None if the value is not a list of 1 to 16 correct names."""
    if not isinstance(value, list) or not 0 < len(value) <= MAX_WATCH_AFFORDANCES:
        return None
    if not all(is_tuning_name(name) for name in value):
        return None
    return tuple(value)


def parse(data):
    """Return the settings from a JSON object. Each incorrect field keeps its default."""
    values = dict(DEFAULTS)
    if not isinstance(data, dict):
        log.write("settings: the file does not hold a JSON object; the mod uses the defaults")
        return values
    if "watch_affordances" in data:
        names = _parse_names(data["watch_affordances"])
        if names is not None:
            values["watch_affordances"] = names
        else:
            log.write("settings: watch_affordances {!r} is not a list of 1 to {} tuning names; the mod uses {}".format(
                data["watch_affordances"], MAX_WATCH_AFFORDANCES, list(DEFAULT_WATCH_AFFORDANCES)))
    enabled = data.get("enabled", True)
    if isinstance(enabled, bool):
        values["enabled"] = enabled
    else:
        log.write("settings: enabled {!r} is not true or false; the mod uses true".format(enabled))
    for name in data:
        if name not in DEFAULTS:
            log.write("settings: the field {!r} is not known".format(name))
    return values


def load(file_path=None):
    """Read the settings file. Return the settings that the mod uses now."""
    global _values
    try:
        if file_path is None:
            file_path = default_path()
        if file_path is None or not os.path.exists(file_path):
            _values = dict(DEFAULTS)
            return dict(_values)
        with open(file_path, "r", encoding="utf-8") as file:
            data = json.load(file)
        _values = parse(data)
    except Exception:  # noqa: BLE001 - incorrect settings must not stop the game.
        log.exception("settings: the mod cannot read {}; the mod uses the defaults".format(file_path))
        _values = dict(DEFAULTS)
    return dict(_values)


def get(name):
    """Return the value of one setting."""
    return _values.get(name, DEFAULTS.get(name))


def set_values(values):
    """Set the settings. The tests use this function."""
    global _values
    _values = dict(DEFAULTS)
    _values.update(values)
