# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The speed of the game for the DLL mod.

The game has no callback for a change of the speed. Each change goes through
clock.GameClock._sync_clock_and_broadcast_gameclock, which sends the time and the speed to the client. The mod wraps
that function: the wrap calls the original first, then sends three values to the DLL mod:

- the ClockSpeedMode value of GameClock.clock_speed (PAUSED 0, NORMAL 1, SPEED2 2, SPEED3 3,
  INTERACTION_STARTUP_SPEED 4, SUPER_SPEED3 5). The mod sends the value as the game has it. The client gets
  SUPER_SPEED3 as SPEED3 with a flag, but the DLL mod gets 5.
- the speed scale from GameClock.current_clock_speed_scale().
- 1 for super speed (the mode is SUPER_SPEED3), else 0.
"""

import functools

from . import log
from . import native

_WRAP_MARK = "_ts4_youtube_tv_wrapped"

# The last values that the mod sent. The cheat command youtube_tv.status shows them.
last_sent = None


def read_speed(game_clock):
    """Return (mode, scale, super_speed) of the game clock."""
    import clock
    mode = int(game_clock.clock_speed)
    scale = float(game_clock.current_clock_speed_scale())
    super_speed = mode == int(clock.ClockSpeedMode.SUPER_SPEED3)
    return mode, scale, super_speed


def send_speed(game_clock):
    """Send the speed of the game clock to the DLL mod."""
    global last_sent
    values = read_speed(game_clock)
    last_sent = values
    native.set_speed(*values)


def _wrap_sync(original):
    @functools.wraps(original)
    def wrapper(self, *args, **kwargs):
        result = original(self, *args, **kwargs)
        try:
            send_speed(self)
        except Exception:  # noqa: BLE001
            log.exception("clock: the wrap of _sync_clock_and_broadcast_gameclock failed")
        return result
    setattr(wrapper, _WRAP_MARK, True)
    return wrapper


def install():
    """Wrap GameClock._sync_clock_and_broadcast_gameclock. A second call does nothing."""
    try:
        import clock
        original = getattr(clock.GameClock, "_sync_clock_and_broadcast_gameclock", None)
        if original is None:
            log.write("clock: GameClock._sync_clock_and_broadcast_gameclock does not exist; the video speed stays 1")
            return False
        if getattr(original, _WRAP_MARK, False):
            return True
        clock.GameClock._sync_clock_and_broadcast_gameclock = _wrap_sync(original)
        return True
    except Exception:  # noqa: BLE001
        log.exception("clock: the wrap of the game clock failed")
        return False
