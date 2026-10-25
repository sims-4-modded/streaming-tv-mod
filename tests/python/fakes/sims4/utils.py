# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake sims4.utils module with flexmethod."""

import functools


class flexmethod:
    """Give the function the class and the instance (None for a call on the class)."""

    def __init__(self, function):
        self.function = function

    def __get__(self, instance, owner):
        return functools.partial(self.function, owner, instance)
