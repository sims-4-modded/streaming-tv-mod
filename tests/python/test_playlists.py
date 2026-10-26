# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the playlist resources: the parser of the SimData and the send to the DLL mod.

The tests make their own SimData bytes. They use no data of the game.
"""

import os
import struct
import unittest

import support
import zone
from sims4 import resources
from ts4_youtube_tv import log
from ts4_youtube_tv import playlists

PLAYLIST = 0x545AC67A
GROUP = 0x00BF458B
VP6 = 0x376840D7
NULL = -0x80000000


def simdata(keys, table_count=1, row_offset=None, keys_offset=None, count=None):
    """Make the SimData of a playlist: a header, one table header, one row with the vector Keys, and the keys.

    The values that are not None replace the correct values, so that a test can make bad data.
    """
    header_size = 24
    table_size = 28
    row_size = 8
    table = header_size
    row = table + table_size
    start = row + row_size
    data = bytearray(start + 16 * len(keys))
    data[0:4] = b"DATA"
    struct.pack_into("<I", data, 4, 0x100)
    struct.pack_into("<iI", data, 8, table - 8, table_count)
    struct.pack_into("<iI", data, 16, NULL, 0)
    # The table header: the name, its hash, the schema, the data type, the row size, the row offset, the row count.
    struct.pack_into("<iIiIIiI", data, table, NULL, 0, NULL, 0, row_size,
                     (row - (table + 0x14)) if row_offset is None else row_offset, 1)
    struct.pack_into("<iI", data, row, (start - row) if keys_offset is None else keys_offset,
                     len(keys) if count is None else count)
    for index, key in enumerate(keys):
        struct.pack_into("<QII", data, start + 16 * index, *key)
    return bytes(data)


ACTION_KEYS = [
    (0xEA6FC4038E0F1EC3, VP6, 0),
    (0xF8C9E12DB961C34A, VP6, 0),
    (0xAF0F3D9C9F8C858A, VP6, 0),
    (0xD8F79C9327A7C43C, VP6, 0),
]


def _log_text():
    path = log.path()
    if not os.path.exists(path):
        return ""
    with open(path, encoding="utf-8") as file:
        return file.read()


class ParseTest(unittest.TestCase):
    def test_reads_the_keys_of_a_playlist(self):
        self.assertEqual(playlists.parse_keys(simdata(ACTION_KEYS)), ACTION_KEYS)
        self.assertEqual(playlists.parse_keys(bytearray(simdata(ACTION_KEYS))), ACTION_KEYS)

    def test_reads_a_playlist_with_no_keys(self):
        self.assertEqual(playlists.parse_keys(simdata([])), [])

    def test_reads_the_offsets_from_their_fields(self):
        # Each offset counts from the position of its own field. Move the keys 16 bytes back with a smaller offset.
        data = bytearray(simdata([(1, VP6, 0), (7, VP6, 9)]))
        row = 24 + 28
        struct.pack_into("<iI", data, row, 8 + 16, 1)
        self.assertEqual(playlists.parse_keys(bytes(data)), [(7, VP6, 9)])

    def test_refuses_bad_data(self):
        bad = [
            b"",
            b"DATA",
            b"ATAD" + simdata(ACTION_KEYS)[4:],
            simdata(ACTION_KEYS, table_count=0),
            simdata(ACTION_KEYS, row_offset=NULL),
            simdata(ACTION_KEYS, row_offset=100000),
            simdata(ACTION_KEYS, keys_offset=NULL),
            simdata(ACTION_KEYS, keys_offset=-1000),
            simdata(ACTION_KEYS, count=5),
            simdata(ACTION_KEYS, count=playlists.MAX_CLIPS + 1),
            simdata(ACTION_KEYS, count=0xFFFFFFFF),
            simdata(ACTION_KEYS)[:60],
        ]
        for data in bad:
            with self.assertRaises(ValueError, msg=data[:16]):
                playlists.parse_keys(data)


class SendTest(unittest.TestCase):
    def setUp(self):
        resources.resources.clear()
        resources.list_error = None
        playlists.last_result = None
        self.dll = support.use_dll(support.FakeDll(version=2))

    def tearDown(self):
        resources.resources.clear()
        resources.list_error = None
        support.use_dll(None)

    def sent(self):
        return [(call[0], call[1], call[2], call[3]) for call in self.dll.YoutubeTvSetPlaylistClips.calls]

    def test_sends_each_playlist(self):
        resources.resources[(PLAYLIST, 0xEFE29AB011F5411A, GROUP)] = simdata(ACTION_KEYS)
        resources.resources[(PLAYLIST, 0x22, GROUP)] = simdata([(0x33, VP6, 0)])
        # Not a playlist, and a playlist with no clips: the mod does not send them.
        resources.resources[(VP6, 0x44, 0)] = b"VP6"
        resources.resources[(PLAYLIST, 0x55, GROUP)] = simdata([])
        self.assertEqual(playlists.send_all(), 2)
        sent = sorted(self.sent())
        self.assertEqual(sent[0][:3], (0x22, PLAYLIST, GROUP))
        self.assertEqual(sent[0][3], struct.pack("<QII", 0x33, VP6, 0))
        self.assertEqual(sent[1][:3], (0xEFE29AB011F5411A, PLAYLIST, GROUP))
        self.assertEqual(len(sent[1][3]), 64)
        self.assertIn("sent 2 of 3 playlists with 5 clips; 0 failed", playlists.last_result)

    def test_a_bad_playlist_does_not_stop_the_others(self):
        resources.resources[(PLAYLIST, 0x11, GROUP)] = b"not SimData"
        resources.resources[(PLAYLIST, 0x12, GROUP)] = KeyError("no resource")
        resources.resources[(PLAYLIST, 0x13, GROUP)] = simdata(ACTION_KEYS)
        before = _log_text().count("playlists: ")
        self.assertEqual(playlists.send_all(), 1)
        self.assertIn("1 of 3 playlists", playlists.last_result)
        self.assertIn("2 failed", playlists.last_result)
        self.assertIn("the first error", playlists.last_result)
        # One log line for the send.
        self.assertEqual(_log_text().count("playlists: "), before + 1)

    def test_a_refused_playlist_counts_as_a_failure(self):
        resources.resources[(PLAYLIST, 0x13, GROUP)] = simdata(ACTION_KEYS)
        self.dll.YoutubeTvSetPlaylistClips.result = 0
        self.dll.error = b"the limit of 256 playlists is full"
        self.assertEqual(playlists.send_all(), 0)
        self.assertIn("1 failed", playlists.last_result)
        self.assertIn("the limit of 256 playlists is full", playlists.last_result)

    def test_sends_at_most_the_limit(self):
        for index in range(playlists.MAX_PLAYLISTS + 3):
            resources.resources[(PLAYLIST, index + 1, GROUP)] = simdata([(index + 1, VP6, 0)])
        self.assertEqual(playlists.send_all(), playlists.MAX_PLAYLISTS)
        self.assertIn("3 over the limit", playlists.last_result)

    def test_a_failed_list_writes_one_line(self):
        resources.list_error = RuntimeError("the resource manager is not ready")
        before = _log_text().count("playlists: not sent")
        self.assertEqual(playlists.send_all(), 0)
        self.assertIn("the resource manager is not ready", playlists.last_result)
        self.assertEqual(_log_text().count("playlists: not sent"), before + 1)
        self.assertEqual(self.sent(), [])

    def test_a_dll_of_version_1_gets_no_playlists(self):
        dll = support.use_dll(support.FakeDll(version=1))
        resources.resources[(PLAYLIST, 0x13, GROUP)] = simdata(ACTION_KEYS)
        self.assertEqual(playlists.send_all(), 0)
        self.assertIn("API version 1", playlists.last_result)
        self.assertFalse(hasattr(dll, "YoutubeTvSetPlaylistClips"))

    def test_no_dll_sends_nothing(self):
        support.use_dll(None)
        resources.resources[(PLAYLIST, 0x13, GROUP)] = simdata(ACTION_KEYS)
        self.assertEqual(playlists.send_all(), 0)

    def test_the_zone_start_sends_the_playlists(self):
        self.assertTrue(getattr(zone.Zone.start_services, "_ts4_youtube_tv_wrapped", False))
        wrapped = zone.Zone.start_services
        playlists.install()
        self.assertIs(zone.Zone.start_services, wrapped)
        resources.resources[(PLAYLIST, 0x13, GROUP)] = simdata(ACTION_KEYS)
        zone.Zone.start_calls.clear()
        zone.Zone().start_services("zone data", "slot data")
        self.assertEqual(zone.Zone.start_calls, [("zone data", "slot data")])
        self.assertEqual(len(self.sent()), 1)

    def test_the_zone_start_continues_after_an_error(self):
        resources.list_error = RuntimeError("bad")
        zone.Zone.start_calls.clear()
        zone.Zone().start_services("zone data", "slot data")
        self.assertEqual(zone.Zone.start_calls, [("zone data", "slot data")])


if __name__ == "__main__":
    unittest.main()
