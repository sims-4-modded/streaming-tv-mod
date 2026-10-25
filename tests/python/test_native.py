# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the bridge to youtube_tv.dll, with a fake DLL."""

import ctypes
import os
import sys
import types
import unittest

import support
from ts4_youtube_tv import log
from ts4_youtube_tv import native


def _log_text():
    path = log.path()
    if not os.path.exists(path):
        return ""
    with open(path, encoding="utf-8") as file:
        return file.read()


class NativeTest(unittest.TestCase):
    def tearDown(self):
        support.use_dll(None)

    def test_turns_off_without_the_dll(self):
        support.use_dll(None)
        before = _log_text().count("the YouTube TV is off")
        self.assertFalse(native.available())
        self.assertFalse(native.available())
        self.assertIn("not loaded", native.reason())
        self.assertEqual(native.play(1, "https://youtu.be/M7lc1UVf-VE", (0, 0, 0), (0, 0, 0, 1))[0], False)
        self.assertFalse(native.stop(1))
        native.stop_all()
        native.set_speed(1, 1.0, False)
        native.set_transform(1, (0, 0, 0), (0, 0, 0, 1))
        native.set_screen_key(1, 2, 3, 4)
        # Only one log line, also after many calls.
        self.assertEqual(_log_text().count("the YouTube TV is off"), before + 1)

    def test_turns_off_for_another_api_version(self):
        dll = support.use_dll(support.FakeDll(version=2))
        self.assertFalse(native.available())
        self.assertIn("API version 2", native.reason())
        native.set_speed(1, 1.0, False)
        self.assertEqual(dll.YoutubeTvSetSpeed.calls, [])

    def test_turns_off_if_the_dll_raises(self):
        def opener():
            raise OSError("bad DLL")
        native.reset(opener=opener)
        self.assertFalse(native.available())

    def test_gives_each_function_its_prototype(self):
        dll = support.use_dll(support.FakeDll())
        self.assertTrue(native.available())
        self.assertEqual(native.api_version(), 1)
        self.assertEqual(dll.YoutubeTvApiVersion.restype, ctypes.c_uint32)
        self.assertEqual(dll.YoutubeTvPlay.argtypes[0], ctypes.c_uint64)
        self.assertEqual(dll.YoutubeTvPlay.argtypes[1], ctypes.c_char_p)
        self.assertEqual(dll.YoutubeTvStopAll.restype, None)
        self.assertEqual(dll.YoutubeTvSetSpeed.argtypes, (ctypes.c_uint32, ctypes.c_double, ctypes.c_uint32))
        self.assertEqual(dll.YoutubeTvSetScreenKey.argtypes,
                         (ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint32, ctypes.c_uint32))
        self.assertEqual(dll.YoutubeTvLastError.argtypes, (ctypes.c_char_p, ctypes.c_uint32))

    def test_sends_the_values_of_play(self):
        dll = support.use_dll(support.FakeDll())
        ok, error = native.play(77, "https://youtu.be/M7lc1UVf-VE?t=5&x=#y", (1, 2, 3), (0, 0, 0.5, 1))
        self.assertTrue(ok)
        self.assertIsNone(error)
        object_id, url, position, orientation = dll.YoutubeTvPlay.calls[0]
        self.assertEqual(object_id, 77)
        self.assertEqual(url, b"https://youtu.be/M7lc1UVf-VE?t=5&x=#y")
        self.assertEqual(list(position), [1.0, 2.0, 3.0])
        self.assertEqual(list(orientation), [0.0, 0.0, 0.5, 1.0])

    def test_gives_the_error_of_the_dll(self):
        support.use_dll(support.FakeDll(play_result=0, error="the helper is not installed".encode("utf-8")))
        ok, error = native.play(1, "https://youtu.be/M7lc1UVf-VE", (0, 0, 0), (0, 0, 0, 1))
        self.assertFalse(ok)
        self.assertEqual(error, "the helper is not installed")

    def test_sends_speed_stop_transform_and_key(self):
        dll = support.use_dll(support.FakeDll())
        native.set_speed(5, 25.0, True)
        self.assertEqual(dll.YoutubeTvSetSpeed.calls, [(5, 25.0, 1)])
        self.assertTrue(native.stop(9))
        self.assertEqual(dll.YoutubeTvStop.calls, [(9,)])
        native.stop_all()
        self.assertEqual(len(dll.YoutubeTvStopAll.calls), 1)
        native.set_screen_key(9, 0x0D432B7F5C14E35D, 0x376840D7, 0)
        self.assertEqual(dll.YoutubeTvSetScreenKey.calls, [(9, 0x0D432B7F5C14E35D, 0x376840D7, 0)])
        native.set_transform(9, (4, 5, 6), (0, 0, 0, 1))
        self.assertEqual(list(dll.YoutubeTvSetTransform.calls[0][1]), [4.0, 5.0, 6.0])

    def test_catches_an_exception_of_a_call(self):
        dll = support.use_dll(support.FakeDll())

        def fail(*args):
            raise OSError("access violation")
        dll.YoutubeTvSetSpeed.side_effect = fail
        native.set_speed(1, 1.0, False)
        dll.YoutubeTvPlay.side_effect = fail
        self.assertFalse(native.play(1, "x", (0, 0, 0), (0, 0, 0, 1))[0])
        self.assertFalse(native.play(1, "x", (0, 0), (0, 0, 0, 1))[0])


class FakeNativeModule(types.ModuleType):
    """A fake Python module ts4_youtube_tv_native, as youtube_tv.dll adds it to sys.modules."""

    def __init__(self, version=1, play_result=1, error=""):
        super().__init__(native.MODULE_NAME)
        self.version = version
        self.play_result = play_result
        self.error = error
        self.calls = []

    def api_version(self):
        return self.version

    def play(self, object_id, url, position, orientation):
        self.calls.append(("play", object_id, url, position, orientation))
        return self.play_result

    def stop(self, object_id):
        self.calls.append(("stop", object_id))
        return 1

    def stop_all(self):
        self.calls.append(("stop_all",))

    def set_speed(self, mode, scale, super_speed):
        self.calls.append(("set_speed", mode, scale, super_speed))

    def set_transform(self, object_id, position, orientation):
        self.calls.append(("set_transform", object_id, position, orientation))

    def set_screen_key(self, object_id, instance, resource_type, group):
        self.calls.append(("set_screen_key", object_id, instance, resource_type, group))

    def last_error(self):
        return self.error

    def status(self):
        return '{"renderCalls": 3}'


class NativeModuleTest(unittest.TestCase):
    def use_module(self, module):
        sys.modules[native.MODULE_NAME] = module
        native.reset()
        return module

    def tearDown(self):
        sys.modules.pop(native.MODULE_NAME, None)
        native.reset()
        support.use_dll(None)

    def test_uses_the_python_module_first(self):
        module = self.use_module(FakeNativeModule())
        self.assertTrue(native.available())
        self.assertEqual(native.api_version(), 1)
        ok, error = native.play(77, "https://youtu.be/M7lc1UVf-VE?t=5&x=#y", (1, 2, 3), (0, 0, 0.5, 1))
        self.assertTrue(ok)
        self.assertIsNone(error)
        self.assertEqual(module.calls[0],
                         ("play", 77, "https://youtu.be/M7lc1UVf-VE?t=5&x=#y", (1.0, 2.0, 3.0), (0.0, 0.0, 0.5, 1.0)))
        native.set_speed(5, 25.0, True)
        self.assertTrue(native.stop(9))
        native.stop_all()
        native.set_screen_key(9, 0x0D432B7F5C14E35D, 0x376840D7, 0)
        native.set_transform(9, (4, 5, 6), (0, 0, 0, 1))
        self.assertEqual([call[0] for call in module.calls[1:]],
                         ["set_speed", "stop", "stop_all", "set_screen_key", "set_transform"])
        self.assertEqual(module.calls[1], ("set_speed", 5, 25.0, 1))

    def test_gives_the_status_of_the_python_module(self):
        self.use_module(FakeNativeModule())
        self.assertEqual(native.status_text(), '{"renderCalls": 3}')
        support.use_dll(support.FakeDll())
        self.assertEqual(native.status_text(), "")

    def test_gives_the_error_of_the_python_module(self):
        self.use_module(FakeNativeModule(play_result=0, error="the helper is not installed"))
        ok, error = native.play(1, "https://youtu.be/M7lc1UVf-VE", (0, 0, 0), (0, 0, 0, 1))
        self.assertFalse(ok)
        self.assertEqual(error, "the helper is not installed")

    def test_turns_off_for_another_api_version_of_the_module(self):
        self.use_module(FakeNativeModule(version=2))
        self.assertFalse(native.available())
        self.assertIn("API version 2", native.reason())

    def test_tries_again_when_the_module_comes_later(self):
        saved = native.ctypes
        native.ctypes = None
        try:
            native.reset()
            self.assertFalse(native.available())
            self.assertIn("has no ctypes", native.reason())
            sys.modules[native.MODULE_NAME] = FakeNativeModule()
            self.assertTrue(native.available())
        finally:
            native.ctypes = saved


if __name__ == "__main__":
    unittest.main()
