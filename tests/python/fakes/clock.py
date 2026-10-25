# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake clock module: ClockSpeedMode and a GameClock that records each sync."""

import enum


class ClockSpeedMode(enum.IntEnum):
    PAUSED = 0
    NORMAL = 1
    SPEED2 = 2
    SPEED3 = 3
    INTERACTION_STARTUP_SPEED = 4
    SUPER_SPEED3 = 5


SCALES = {
    ClockSpeedMode.PAUSED: 0.0,
    ClockSpeedMode.NORMAL: 1.0,
    ClockSpeedMode.SPEED2: 3.0,
    ClockSpeedMode.SPEED3: 7.0,
    ClockSpeedMode.INTERACTION_STARTUP_SPEED: 3.5,
    ClockSpeedMode.SUPER_SPEED3: 25.0,
}


class GameClock:
    def __init__(self):
        self.clock_speed = ClockSpeedMode.PAUSED
        self.events = []

    def current_clock_speed_scale(self):
        return SCALES[self.clock_speed]

    def _sync_clock_and_broadcast_gameclock(self, immediate=False):
        self.events.append(("sync", int(self.clock_speed), immediate))
