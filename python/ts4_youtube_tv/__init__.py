# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The script mod of the YouTube TV.

The game imports this package when it loads ts4_youtube_tv.ts4script from the Mods folder. The import reads the
settings, wraps the game functions (the clock, the Video component, the zone teardown), registers the injection of the
interaction, and registers the cheat commands. An error in one part writes a log line and does not stop the other
parts or the game.
"""

VERSION = "0.1.0"


def _start():
    from . import log
    log.write("YouTube TV script mod {} starts".format(VERSION))
    steps = (
        ("settings", "settings", "load"),
        ("clock", "clock", "install"),
        ("session", "session", "install"),
        ("injection", "injection", "install"),
        ("commands", "commands", None),
        ("interaction", "interaction", None),
    )
    import importlib
    for label, module_name, function_name in steps:
        try:
            module = importlib.import_module("{}.{}".format(__name__, module_name))
            if function_name is not None:
                getattr(module, function_name)()
        except Exception:  # noqa: BLE001 - one bad part must not stop the game.
            log.exception("start: the part {} failed".format(label))


try:
    _start()
except Exception:  # noqa: BLE001
    pass
