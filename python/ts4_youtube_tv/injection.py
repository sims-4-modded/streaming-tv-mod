# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""Add the interaction to the pie menu of each object with a Video component.

When the OBJECT instance manager completes its load, the mod adds the tuned interaction (from the package of the mod)
to _super_affordances of each object tuning class with a Video component. The test of the interaction then shows it
only on TVs (the tag Func_TV), not on computers, tablets or consoles. A second run adds nothing.
"""

from . import log

_installed = False


def has_video_component(tuning_class):
    """Return True if the object tuning class has a tuned Video component."""
    components = getattr(tuning_class, "_components", None)
    if components is None:
        return False
    return getattr(components, "Video", None) is not None


def find_affordance(instance_id):
    """Return the interaction tuning class of the mod, or None."""
    import services
    import sims4.resources
    manager = services.get_instance_manager(sims4.resources.Types.INTERACTION)
    if manager is None:
        return None
    return manager.get(instance_id)


def add_to_object_classes(object_manager, affordance):
    """Add the affordance to each object class with a Video component. Return the count of changed classes."""
    changed = 0
    for tuning_class in list(object_manager.types.values()):
        try:
            if not has_video_component(tuning_class):
                continue
            affordances = getattr(tuning_class, "_super_affordances", None)
            if affordances is None:
                continue
            if affordance in affordances:
                continue
            tuning_class._super_affordances = tuple(affordances) + (affordance,)
            changed += 1
        except Exception:  # noqa: BLE001
            log.exception("injection: the object class {} failed".format(getattr(tuning_class, "__name__", "?")))
    return changed


def on_object_load_complete(object_manager):
    """The callback of the OBJECT instance manager. It never raises an exception."""
    try:
        from .interaction import INSTANCE_ID
        affordance = find_affordance(INSTANCE_ID)
        if affordance is None:
            log.write("injection: the interaction tuning {:#018x} is not loaded; is ts4_youtube_tv.package "
                      "installed?".format(INSTANCE_ID))
            return
        changed = add_to_object_classes(object_manager, affordance)
        log.write("injection: added the interaction to {} object classes with a Video component".format(changed))
    except Exception:  # noqa: BLE001
        log.exception("injection: the injection failed")


def install():
    """Register the callback with the OBJECT instance manager. A second call does nothing."""
    global _installed
    if _installed:
        return True
    try:
        import services
        import sims4.resources
        manager = services.get_instance_manager(sims4.resources.Types.OBJECT)
        if manager is None:
            log.write("injection: the OBJECT instance manager does not exist")
            return False
        manager.add_on_load_complete(on_object_load_complete)
        _installed = True
        return True
    except Exception:  # noqa: BLE001
        log.exception("injection: the registration failed")
        return False


def reset():
    """Forget the registration. The tests use this function."""
    global _installed
    _installed = False
