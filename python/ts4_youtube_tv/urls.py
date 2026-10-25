# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The check of the URL of a YouTube video.

The rules are the same as the rules of the parser of the DLL mod (src/core/YoutubeUrl.cpp). The script mod checks the
text before it sends the text to the DLL mod, so that the player gets a fast and clear error. Keep the two parsers the
same: the tests of the two parsers use the same examples.

The module accepts these forms, with or without https:// and with other parameters:

- youtube.com/watch?v=<id> (also www., m. and music.)
- youtu.be/<id>
- youtube.com/shorts/<id>, youtube.com/embed/<id>, youtube.com/live/<id>, youtube.com/v/<id>
- youtube-nocookie.com/embed/<id>
- the id of 11 characters alone
"""

import collections

VIDEO_ID_LENGTH = 11

# The start time can be 100 days at the maximum, as in the DLL mod.
MAX_START_SECONDS = 24 * 60 * 60 * 100

_YOUTUBE_HOSTS = (
    "youtube.com",
    "www.youtube.com",
    "m.youtube.com",
    "music.youtube.com",
    "youtube-nocookie.com",
    "www.youtube-nocookie.com",
)
_SHORT_HOSTS = ("youtu.be", "www.youtu.be")
_ID_PATH_PREFIXES = ("/shorts/", "/embed/", "/live/", "/v/")
_ID_CHARACTERS = frozenset("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")
_SPACE = " \t\r\n"

YoutubeVideo = collections.namedtuple("YoutubeVideo", ("id", "start_seconds"))


class UrlError(ValueError):
    """The text is not the URL of a YouTube video. The text of the error tells the player why."""


def _ascii_lower(text):
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in text)


def _find_first_of(text, characters):
    """Return the index of the first character of the text that is in characters, or -1."""
    for index, c in enumerate(text):
        if c in characters:
            return index
    return -1


def is_video_id(text):
    """Return True if the text is a correct video id: 11 characters from A-Z, a-z, 0-9, - and _."""
    return isinstance(text, str) and len(text) == VIDEO_ID_LENGTH and all(c in _ID_CHARACTERS for c in text)


def parse_start_time(text):
    """Return the seconds of a start time: 90, 90s, 1m30s or 1h2m3s. Raise UrlError for other text."""
    if not text:
        raise UrlError("the start time is empty")
    total = 0
    number = 0
    has_digits = False
    for c in text:
        if "0" <= c <= "9":
            number = number * 10 + (ord(c) - ord("0"))
            has_digits = True
            if number > MAX_START_SECONDS:
                raise UrlError("the start time is too large")
            continue
        if not has_digits:
            raise UrlError('the start time "{}" is not correct'.format(text))
        if c == "h":
            unit = 3600
        elif c == "m":
            unit = 60
        elif c == "s":
            unit = 1
        else:
            raise UrlError('the start time "{}" is not correct'.format(text))
        total += number * unit
        number = 0
        has_digits = False
    total += number
    if total > MAX_START_SECONDS:
        raise UrlError("the start time is too large")
    return total


def _query_value(query, name):
    """Return the value of one parameter of the query, or None if the query does not have the parameter."""
    while query:
        end = query.find("&")
        pair = query if end < 0 else query[:end]
        equals = pair.find("=")
        key = pair if equals < 0 else pair[:equals]
        if key == name:
            return "" if equals < 0 else pair[equals + 1:]
        if end < 0:
            break
        query = query[end + 1:]
    return None


def _id_from_path_rest(rest):
    """Return the id from the part of a path after a prefix, or None. One / after the id is permitted."""
    if rest.endswith("/"):
        rest = rest[:-1]
    return rest if is_video_id(rest) else None


def _start_from_query(query):
    """Return the start time of the query, or 0 if the query does not have a correct start time."""
    for name in ("t", "start"):
        value = _query_value(query, name)
        if value is not None:
            try:
                return parse_start_time(value)
            except UrlError:
                pass
    return 0


def parse_youtube_video(text):
    """Return the YoutubeVideo of a URL or of a video id. Raise UrlError for all other text."""
    if not isinstance(text, str):
        raise UrlError("the text is not a URL")
    text = text.strip(_SPACE)
    if is_video_id(text):
        return YoutubeVideo(text, 0)

    rest = text
    for scheme in ("https://", "http://"):
        if _ascii_lower(rest[:len(scheme)]) == scheme:
            rest = rest[len(scheme):]
            break

    host_end = _find_first_of(rest, "/?#")
    host = _ascii_lower(rest if host_end < 0 else rest[:host_end])
    if not host or ":" in host or "@" in host:
        raise UrlError('the text "{}" is not the URL of a YouTube video'.format(text))
    rest = "" if host_end < 0 else rest[host_end:]
    fragment = rest.find("#")
    if fragment >= 0:
        rest = rest[:fragment]
    query_start = rest.find("?")
    path = rest if query_start < 0 else rest[:query_start]
    query = "" if query_start < 0 else rest[query_start + 1:]

    video_id = None
    if host in _SHORT_HOSTS:
        if path.startswith("/"):
            video_id = _id_from_path_rest(path[1:])
    elif host in _YOUTUBE_HOSTS:
        if path in ("/watch", "/watch/"):
            value = _query_value(query, "v")
            if value is not None and is_video_id(value):
                video_id = value
        for prefix in _ID_PATH_PREFIXES:
            if video_id is None and path.startswith(prefix):
                video_id = _id_from_path_rest(path[len(prefix):])
    else:
        raise UrlError('the site "{}" is not YouTube'.format(host))

    if video_id is None:
        raise UrlError('the URL "{}" does not have the id of a YouTube video'.format(text))
    return YoutubeVideo(video_id, _start_from_query(query))


def is_youtube_video_url(text):
    """Return True if the text is the URL of a YouTube video or a video id."""
    try:
        parse_youtube_video(text)
    except UrlError:
        return False
    return True
