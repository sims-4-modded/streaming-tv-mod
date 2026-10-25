# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake sims4.commands module. It keeps the commands and the console output."""

import enum

registry = {}
outputs = []


class CommandType(enum.IntEnum):
    DebugOnly = 0
    Live = 5


def Command(*aliases, command_type=CommandType.DebugOnly, **kwargs):
    def decorator(function):
        for alias in aliases:
            registry[alias] = (function, command_type)
        return function
    return decorator


def output(text, connection):
    outputs.append(text)
