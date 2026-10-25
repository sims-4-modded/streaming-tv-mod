# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The bridge from the script mod to the DLL mod youtube_tv.dll (bridge version 1).

Only this module knows the C interface of the DLL mod. When the extender gets its native Python module, only this
module changes.

The extender loads youtube_tv.dll. This module does not load a second copy: it gets the handle of the loaded module
and makes a ctypes library from that handle. If the DLL is not loaded, or its API version is not 1, the bridge turns
off with one log line. Then each function of this module does nothing and returns a failure.

Each function of this module catches each exception, writes it to the log, and returns a failure. The game Python
calls these functions on the main thread, as the bridge needs.
"""

import ctypes

from . import log

DLL_NAME = "youtube_tv.dll"
API_VERSION = 1

_ERROR_BUFFER_SIZE = 1024

_dll = None
_loaded = False
_reason = "the bridge did not start"
_api_version = None


def _open_dll():
    """Return a ctypes library for the youtube_tv.dll that the extender loaded, or (None, reason)."""
    if not hasattr(ctypes, "WinDLL"):
        return None, "ctypes has no WinDLL on this system"
    # A private kernel32 object, so that the prototypes of this module do not change those of other mods.
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    get_module_handle = kernel32.GetModuleHandleW
    get_module_handle.argtypes = (ctypes.c_wchar_p,)
    get_module_handle.restype = ctypes.c_void_p
    handle = get_module_handle(DLL_NAME)
    if not handle:
        return None, "{} is not loaded (the extender or the DLL mod is not installed)".format(DLL_NAME)
    return ctypes.CDLL(DLL_NAME, handle=handle), None


def _set_prototypes(dll):
    """Give each function of the DLL its argument types and its result type."""
    c_float_p = ctypes.POINTER(ctypes.c_float)
    prototypes = (
        ("YoutubeTvApiVersion", (), ctypes.c_uint32),
        ("YoutubeTvPlay", (ctypes.c_uint64, ctypes.c_char_p, c_float_p, c_float_p), ctypes.c_uint32),
        ("YoutubeTvStop", (ctypes.c_uint64,), ctypes.c_uint32),
        ("YoutubeTvStopAll", (), None),
        ("YoutubeTvSetSpeed", (ctypes.c_uint32, ctypes.c_double, ctypes.c_uint32), None),
        ("YoutubeTvSetTransform", (ctypes.c_uint64, c_float_p, c_float_p), None),
        ("YoutubeTvSetScreenKey", (ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint32, ctypes.c_uint32), None),
        ("YoutubeTvLastError", (ctypes.c_char_p, ctypes.c_uint32), ctypes.c_uint32),
    )
    for name, argtypes, restype in prototypes:
        function = getattr(dll, name)
        function.argtypes = argtypes
        function.restype = restype


def reset(opener=None):
    """Forget the DLL. The next call loads it again with the opener (the tests give a fake opener)."""
    global _dll, _loaded, _reason, _api_version, _open_dll_function
    _dll = None
    _loaded = False
    _reason = "the bridge did not start"
    _api_version = None
    _open_dll_function = opener if opener is not None else _open_dll


_open_dll_function = _open_dll


def load():
    """Load the bridge once. Return True if the DLL is ready. Write one log line for the result."""
    global _dll, _loaded, _reason, _api_version
    if _loaded:
        return _dll is not None
    _loaded = True
    try:
        dll, reason = _open_dll_function()
        if dll is None:
            _reason = reason
            log.write("native: the YouTube TV is off: {}".format(reason))
            return False
        _set_prototypes(dll)
        _api_version = int(dll.YoutubeTvApiVersion())
        if _api_version != API_VERSION:
            _reason = "{} has the API version {}, but the script mod needs {}".format(
                DLL_NAME, _api_version, API_VERSION)
            log.write("native: the YouTube TV is off: {}".format(_reason))
            return False
        _dll = dll
        _reason = None
        log.write("native: {} is ready (API version {})".format(DLL_NAME, _api_version))
        return True
    except Exception:  # noqa: BLE001 - a bad DLL must not stop the game.
        _reason = "the bridge to {} failed".format(DLL_NAME)
        log.exception("native: the YouTube TV is off: {}".format(_reason))
        _dll = None
        return False


def available():
    """Return True if the DLL is ready. The first call loads the bridge."""
    return load()


def reason():
    """Return the reason why the bridge is off, or None if it is ready."""
    load()
    return _reason


def api_version():
    """Return the API version of the loaded DLL, or None."""
    load()
    return _api_version


def _vector(values, count):
    values = tuple(float(v) for v in values)
    if len(values) != count:
        raise ValueError("the vector must have {} values".format(count))
    return (ctypes.c_float * count)(*values)


def last_error():
    """Return the last error of the DLL as text, or an empty text."""
    if not available():
        return _reason or ""
    try:
        buffer = ctypes.create_string_buffer(_ERROR_BUFFER_SIZE)
        length = int(_dll.YoutubeTvLastError(buffer, _ERROR_BUFFER_SIZE))
        length = max(0, min(length, _ERROR_BUFFER_SIZE - 1))
        return buffer.raw[:length].decode("utf-8", "replace")
    except Exception:  # noqa: BLE001
        log.exception("native: YoutubeTvLastError failed")
        return ""


def play(object_id, url, position, orientation):
    """Start the video of the URL on the TV. Return (True, None) or (False, the error)."""
    if not available():
        return False, _reason
    try:
        result = _dll.YoutubeTvPlay(int(object_id), url.encode("utf-8"), _vector(position, 3),
                                    _vector(orientation, 4))
        if int(result) == 1:
            return True, None
        error = last_error() or "the DLL mod did not start the video"
        log.write("native: YoutubeTvPlay failed for the object {}: {}".format(object_id, error))
        return False, error
    except Exception:  # noqa: BLE001
        log.exception("native: YoutubeTvPlay failed for the object {}".format(object_id))
        return False, "the bridge to the DLL mod failed"


def stop(object_id):
    """Stop the video of the TV. Return True if a video stopped."""
    if not available():
        return False
    try:
        return int(_dll.YoutubeTvStop(int(object_id))) == 1
    except Exception:  # noqa: BLE001
        log.exception("native: YoutubeTvStop failed for the object {}".format(object_id))
        return False


def stop_all():
    """Stop every video."""
    if not available():
        return
    try:
        _dll.YoutubeTvStopAll()
    except Exception:  # noqa: BLE001
        log.exception("native: YoutubeTvStopAll failed")


def set_speed(clock_speed_mode, speed_scale, super_speed):
    """Send the clock of the game: the ClockSpeedMode value, the speed scale, and True for super speed."""
    if not available():
        return
    try:
        _dll.YoutubeTvSetSpeed(int(clock_speed_mode), float(speed_scale), 1 if super_speed else 0)
    except Exception:  # noqa: BLE001
        log.exception("native: YoutubeTvSetSpeed failed")


def set_transform(object_id, position, orientation):
    """Send the new position (x, y, z) and orientation (quaternion x, y, z, w) of a TV."""
    if not available():
        return
    try:
        _dll.YoutubeTvSetTransform(int(object_id), _vector(position, 3), _vector(orientation, 4))
    except Exception:  # noqa: BLE001
        log.exception("native: YoutubeTvSetTransform failed for the object {}".format(object_id))


def set_screen_key(object_id, instance, resource_type, group):
    """Send the key of the clip that the script mod put on the TV."""
    if not available():
        return
    try:
        _dll.YoutubeTvSetScreenKey(int(object_id), int(instance), int(resource_type), int(group))
    except Exception:  # noqa: BLE001
        log.exception("native: YoutubeTvSetScreenKey failed for the object {}".format(object_id))
