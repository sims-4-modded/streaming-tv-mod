# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The Sim watches the TV: the mod pushes the watch interaction of the game on the Sim.

The game has one watch interaction for each channel of a TV (the class WatchSuperInteraction in
objects.electronics.television). The interaction makes the Sim go to the TV, puts the TV on its channel, and lets the
Sim sit or stand and watch. When it ends, the last Sim puts the TV off. A change of the channel by the player cancels
it. The mod uses the interaction of the game as it is: it does not copy or change it.

The setting watch_affordances gives the names of the watch interactions in the order that the mod tries them. If no
name matches, the mod takes the first watch interaction of the TV.
"""

from . import log
from . import settings


def _watch_class():
    """Return the class WatchSuperInteraction of the game, or None."""
    try:
        from objects.electronics.television import WatchSuperInteraction
        return WatchSuperInteraction
    except Exception:  # noqa: BLE001
        log.exception("watch: the game has no WatchSuperInteraction")
        return None


def watch_affordances(obj):
    """Return the watch interactions of the TV, in the order of the game."""
    watch_class = _watch_class()
    if watch_class is None:
        return []
    super_affordances = getattr(obj, "super_affordances", None)
    if super_affordances is None:
        return []
    found = []
    for affordance in super_affordances():
        try:
            if isinstance(affordance, type) and issubclass(affordance, watch_class) and affordance not in found:
                found.append(affordance)
        except Exception:  # noqa: BLE001
            continue
    return found


def find_watch_affordance(obj):
    """Return the watch interaction that the mod uses for the TV, or None."""
    candidates = watch_affordances(obj)
    if not candidates:
        return None
    by_name = {getattr(affordance, "__name__", ""): affordance for affordance in candidates}
    for name in settings.get("watch_affordances"):
        if name in by_name:
            return by_name[name]
    return candidates[0]


def push_watch(sim, obj):
    """Push the watch interaction of the TV on the Sim. Return (the interaction, None) or (None, the error)."""
    if sim is None:
        return None, "there is no Sim to watch the TV"
    affordance = find_watch_affordance(obj)
    if affordance is None:
        return None, "this TV has no watch interaction"
    from interactions.context import InteractionContext
    from interactions.priority import Priority
    context = InteractionContext(sim, InteractionContext.SOURCE_SCRIPT_WITH_USER_INTENT, Priority.High)
    result = sim.push_super_affordance(affordance, obj, context)
    interaction = getattr(result, "interaction", None)
    if not result or interaction is None:
        return None, "the Sim cannot watch this TV now ({})".format(getattr(affordance, "__name__", "?"))
    log.write("watch: pushed {} on the Sim {} for the object {}".format(
        getattr(affordance, "__name__", "?"), getattr(sim, "id", "?"), getattr(obj, "id", "?")))
    return interaction, None
