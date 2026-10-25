# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake services module: the instance managers, the active Sim and the object manager."""

instance_managers = {}
active_sim = None
objects = None


def get_instance_manager(resource_type):
    return instance_managers.get(resource_type)


def get_active_sim():
    return active_sim


def object_manager(zone_id=None):
    return objects


def reset():
    global active_sim, objects
    instance_managers.clear()
    active_sim = None
    objects = None
