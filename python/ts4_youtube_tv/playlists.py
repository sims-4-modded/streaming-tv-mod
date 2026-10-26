# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The clips of the playlist resources of the game, for the link of a TV channel in the DLL mod.

The game gives a TV a list with the key of a playlist resource (type PLAYLIST). The channel of the TV does not play
that key: it plays the VP6 clips of the resource in a random order. The DLL mod sees only the clip of the channel.
Thus the script mod reads the clips of each playlist resource and gives them to the DLL mod. Then the DLL mod can link
the channel to the TV whose list has the playlist key.

The resource is SimData. The parser reads only what the game reads: the first table, the first row of that table,
and the vector Keys at the start of the row. Each key has 16 bytes: a u64 instance, a u32 type and a u32 group.

The wrap of zone.Zone.start_services calls the original first, then sends the playlists. The zone loads its objects
after start_services, so the DLL mod has the clips before a TV gets its first list.

If a step fails, the mod writes one log line and the DLL mod uses its time rule.
"""

import functools
import struct

from . import log
from . import native

_WRAP_MARK = "_ts4_youtube_tv_wrapped"

# The type of all SimData resources. Only the resources of the group PLAYLIST_GROUP are playlists.
PLAYLIST_TYPE = 0x545AC67A
PLAYLIST_GROUP = 0x00BF458B

# The limits of the DLL mod. The game has approximately 24 playlists with 76 clips.
MAX_PLAYLISTS = 256
MAX_CLIPS = 1024

_MAGIC = b"DATA"
_HEADER_SIZE = 24
_TABLE_HEADER_SIZE = 28
# The field of the row offset in a table header.
_TABLE_ROW_FIELD = 0x14
_KEY_SIZE = 16
# A relative offset with this value is null.
_NULL_OFFSET = -0x80000000

# The result of the last send, for the cheat command youtube_tv.status.
last_result = None


def _read(data, fmt, position):
    size = struct.calcsize(fmt)
    if position < 0 or position + size > len(data):
        raise ValueError("the resource has no {} bytes at {}".format(size, position))
    return struct.unpack_from(fmt, data, position)[0]


def _relative(data, position):
    """Return the position that the relative offset at this position points to."""
    offset = _read(data, "<i", position)
    if offset == _NULL_OFFSET:
        raise ValueError("the offset at {} is null".format(position))
    target = position + offset
    if target < 0 or target > len(data):
        raise ValueError("the offset at {} points out of the resource".format(position))
    return target


def parse_keys(data):
    """Return the clip keys (instance, type, group) of the SimData of a playlist resource.

    Raise ValueError if the data is not correct or has more than MAX_CLIPS keys.
    """
    data = bytes(data)
    if len(data) < _HEADER_SIZE or data[:4] != _MAGIC:
        raise ValueError("the resource is not SimData")
    table = _relative(data, 8)
    if _read(data, "<I", 12) < 1:
        raise ValueError("the SimData has no table")
    if table + _TABLE_HEADER_SIZE > len(data):
        raise ValueError("the table header is out of the resource")
    row = _relative(data, table + _TABLE_ROW_FIELD)
    count = _read(data, "<I", row + 4)
    if count > MAX_CLIPS:
        raise ValueError("the playlist has {} keys, more than the limit of {}".format(count, MAX_CLIPS))
    if count == 0:
        return []
    start = _relative(data, row)
    if start + count * _KEY_SIZE > len(data):
        raise ValueError("the {} keys are out of the resource".format(count))
    return [struct.unpack_from("<QII", data, start + index * _KEY_SIZE) for index in range(count)]


def _key_tuple(key):
    return int(key.instance), int(key.type), int(key.group)


def send_all():
    """Read each playlist resource of the game and send its clips to the DLL mod.

    Return the number of playlists that the DLL mod kept. Write one log line with the result.
    """
    global last_result
    try:
        if not native.available():
            return 0
        if not native.has_playlist_clips():
            last_result = "the DLL mod has API version {}, with no playlist link".format(native.api_version())
            log.write("playlists: not sent: {}".format(last_result))
            return 0
        import sims4.resources
        playlist_type = getattr(sims4.resources.Types, "PLAYLIST", PLAYLIST_TYPE)
        # The game has many SimData resources of this type. Keep only the playlists.
        keys = [key for key in sims4.resources.list(type=playlist_type, group=PLAYLIST_GROUP)
                if int(key.group) == PLAYLIST_GROUP]
    except Exception as error:  # noqa: BLE001 - the DLL mod then uses its time rule.
        last_result = "the list of the playlist resources failed: {!r}".format(error)
        log.write("playlists: not sent: {}".format(last_result))
        return 0
    sent = 0
    clips = 0
    failed = 0
    first_error = None
    for key in keys[:MAX_PLAYLISTS]:
        try:
            playlist = _key_tuple(key)
            clip_keys = parse_keys(sims4.resources.load(key))
            if not clip_keys:
                continue
            ok, error = native.set_playlist_clips(playlist, clip_keys)
            if not ok:
                raise ValueError(error)
            sent += 1
            clips += len(clip_keys)
        except Exception as error:  # noqa: BLE001
            failed += 1
            if first_error is None:
                first_error = "{!r}: {}".format(key, error)
    skipped = max(0, len(keys) - MAX_PLAYLISTS)
    last_result = "sent {} of {} playlists with {} clips; {} failed; {} over the limit".format(
        sent, len(keys), clips, failed, skipped)
    if first_error is not None:
        last_result += "; the first error: {}".format(first_error)
    log.write("playlists: {}".format(last_result))
    return sent


def _wrap_start_services(original):
    @functools.wraps(original)
    def wrapper(self, *args, **kwargs):
        result = original(self, *args, **kwargs)
        try:
            send_all()
        except Exception:  # noqa: BLE001
            log.exception("playlists: the wrap of Zone.start_services failed")
        return result
    setattr(wrapper, _WRAP_MARK, True)
    return wrapper


def install():
    """Wrap zone.Zone.start_services. A second call does nothing."""
    import zone
    original = getattr(zone.Zone, "start_services", None)
    if original is None:
        log.write("playlists: Zone.start_services does not exist; the DLL mod uses its time rule")
        return False
    if not getattr(original, _WRAP_MARK, False):
        zone.Zone.start_services = _wrap_start_services(original)
    return True
