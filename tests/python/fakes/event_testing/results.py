# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake event_testing.results module."""


class TestResult:
    __test__ = False

    def __init__(self, result, *args, **kwargs):
        self.result = result
        self.reason = args[0] if args else None

    def __bool__(self):
        return bool(self.result)
