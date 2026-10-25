# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The bridge from the script mod to the DLL mod youtube_tv.dll (bridge version 1).

Only this module knows the C interface of the DLL mod. When the extender gets its native Python module, only this
module changes.

The extender loads youtube_tv.dll. The game Python has no ctypes, so the DLL adds the Python module
ts4_youtube_tv_native to sys.modules. This module uses that Python module first. If the Python module is not there and
ctypes is there, this module gets the handle of the loaded DLL and makes a ctypes library from that handle. It does
not load a second copy of the DLL. If neither works, or the API version is not 1, the bridge turns off with one log
line. Then each function of this module does nothing and returns a failure. If the DLL is not there yet, the next
call tries again.

Each function of this module catches each exception, writes it to the log, and returns a failure. The game Python
calls these functions on the main thread, as the bridge needs.
"""

import importlib

from . import log

try:
    import ctypes
except ImportError:
    # The game Python has no ctypes. The bridge then uses the Python module of the DLL.
    ctypes = None

DLL_NAME = "youtube_tv.dll"
MODULE_NAME = "ts4_youtube_tv_native"
API_VERSION = 1

_ERROR_BUFFER_SIZE = 1024

_dll = None
_loaded = False
_reason = "the bridge did not start"
_api_version = None
_absent_logged = False


class _ModuleLibrary:
    """The functions of the Python module of the DLL, with the names and the arguments of the C interface."""

    uses_ctypes = False

    def __init__(self, module):
        self._module = module

    def YoutubeTvApiVersion(self):
        return self._module.api_version()

    def YoutubeTvPlay(self, object_id, url, position, orientation):
        return self._module.play(object_id, url.decode("utf-8"), tuple(position), tuple(orientation))

    def YoutubeTvStop(self, object_id):
        return self._module.stop(object_id)

    def YoutubeTvStopAll(self):
        self._module.stop_all()

    def YoutubeTvSetSpeed(self, mode, scale, super_speed):
        self._module.set_speed(mode, scale, super_speed)

    def YoutubeTvSetTransform(self, object_id, position, orientation):
        self._module.set_transform(object_id, tuple(position), tuple(orientation))

    def YoutubeTvSetScreenKey(self, object_id, instance, resource_type, group):
        self._module.set_screen_key(object_id, instance, resource_type, group)

    def last_error_text(self):
        return self._module.last_error()

    def status_text(self):
        return self._module.status()


def _open_dll():
    """Return a library for the youtube_tv.dll that the extender loaded, or (None, reason)."""
    try:
        return _ModuleLibrary(importlib.import_module(MODULE_NAME)), None
    except ImportError:
        pass
    if ctypes is None:
        return None, "the Python module {} is not there and the game Python has no ctypes".format(MODULE_NAME)
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
    """Give each function of the DLL its argument types and its result type. The Python module needs no types."""
    if not getattr(dll, "uses_ctypes", True):
        return
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
    global _dll, _loaded, _reason, _api_version, _open_dll_function, _absent_logged
    _dll = None
    _loaded = False
    _absent_logged = False
    _reason = "the bridge did not start"
    _api_version = None
    _open_dll_function = opener if opener is not None else _open_dll


_open_dll_function = _open_dll


def load():
    """Load the bridge once. Return True if the DLL is ready. Write one log line for the result."""
    global _dll, _loaded, _reason, _api_version, _absent_logged
    if _loaded:
        return _dll is not None
    try:
        dll, reason = _open_dll_function()
        if dll is None:
            # The DLL can come later, so the next call tries again. Only the first failure writes the log.
            _reason = reason
            if not _absent_logged:
                _absent_logged = True
                log.write("native: the YouTube TV is off: {}".format(reason))
            return False
        _loaded = True
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
        _loaded = True
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
    if not getattr(_dll, "uses_ctypes", True):
        return values
    return (ctypes.c_float * count)(*values)


def last_error():
    """Return the last error of the DLL as text, or an empty text."""
    if not available():
        return _reason or ""
    try:
        if not getattr(_dll, "uses_ctypes", True):
            return _dll.last_error_text()
        buffer = ctypes.create_string_buffer(_ERROR_BUFFER_SIZE)
        length = int(_dll.YoutubeTvLastError(buffer, _ERROR_BUFFER_SIZE))
        length = max(0, min(length, _ERROR_BUFFER_SIZE - 1))
        return buffer.raw[:length].decode("utf-8", "replace")
    except Exception:  # noqa: BLE001
        log.exception("native: YoutubeTvLastError failed")
        return ""


def status_text():
    """Return the status of the DLL mod as JSON text, or an empty text. Only the Python module of the DLL gives it."""
    if not available():
        return ""
    status = getattr(_dll, "status_text", None)
    if status is None:
        return ""
    try:
        return str(status())
    except Exception:  # noqa: BLE001
        log.exception("native: the status of the DLL failed")
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
