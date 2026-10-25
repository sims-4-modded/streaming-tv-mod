# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The TV objects of the game: the tests for a TV, its Video component, its transform and the screen clip key.

A TV is an object with the catalogue tag Func_TV. Computers, tablets and consoles also have a Video component, so the
mod tests the tag too.
"""

from . import fnv
from . import log
from . import settings

# The resource type of a video clip (VP6Video). The game puts the clips in group 0.
VIDEO_CLIP_TYPE = 0x376840D7
VIDEO_CLIP_GROUP = 0

_TV_TAG_NAMES = ("Func_TV", "Func_Television")


def screen_clip_name():
    """Return the name of the clip that the mod puts on the TV."""
    return settings.get("screen_clip")


def clip_key(clip_name):
    """Return (instance, type, group) of a clip name, as the game makes the key in VideoPlaylist."""
    return fnv.fnv1_64(clip_name), VIDEO_CLIP_TYPE, VIDEO_CLIP_GROUP


def _tv_tags():
    import tag
    tags = []
    for name in _TV_TAG_NAMES:
        value = getattr(tag.Tag, name, None)
        if value is not None:
            tags.append(value)
    return tags


def video_component(obj):
    """Return the Video component of the object, or None."""
    if obj is None:
        return None
    from objects.components import types
    get_component = getattr(obj, "get_component", None)
    if get_component is None:
        return None
    return get_component(types.VIDEO_COMPONENT)


def is_tv(obj):
    """Return True if the object has a TV tag and a Video component."""
    if obj is None or getattr(obj, "is_sim", False):
        return False
    has_tag = getattr(obj, "has_tag", None)
    if has_tag is None:
        return False
    if not any(has_tag(value) for value in _tv_tags()):
        return False
    return video_component(obj) is not None


def is_broken(obj):
    """Return True if the state component of the object says that the object is broken."""
    state_component = getattr(obj, "state_component", None)
    if state_component is None:
        return False
    is_object_usable = getattr(state_component, "is_object_usable", None)
    if is_object_usable is None:
        return False
    return not is_object_usable()


def is_usable_tv(obj):
    """Return True if the object is a TV that is not broken. An exception gives False and a log line."""
    try:
        return is_tv(obj) and not is_broken(obj)
    except Exception:  # noqa: BLE001
        log.exception("tv: the test of the object failed")
        return False


def transform(obj):
    """Return ((x, y, z), (x, y, z, w)): the position and the orientation of the object."""
    position = obj.position
    orientation = obj.orientation
    return ((float(position.x), float(position.y), float(position.z)),
            (float(orientation.x), float(orientation.y), float(orientation.z), float(orientation.w)))


def distance_squared(a, b):
    """Return the square of the distance between two positions."""
    return (a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2


def usable_tvs():
    """Return the TVs of the zone that are not broken."""
    import services
    manager = services.object_manager()
    if manager is None:
        return []
    return [obj for obj in manager.valid_objects() if is_usable_tv(obj)]


def nearest_tv(sim):
    """Return the TV that is nearest to the Sim, or None."""
    tvs = usable_tvs()
    if not tvs:
        return None
    if sim is None:
        return tvs[0]
    return min(tvs, key=lambda obj: distance_squared(obj.position, sim.position))
