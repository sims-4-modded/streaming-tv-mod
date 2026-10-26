# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake sims4.resources module. The tests put the resources into the dictionary resources."""


class Types:
    OBJECT = 0x319E4F1D
    INTERACTION = 0xE882D22F
    PLAYLIST = 0x545AC67A


class Key:
    def __init__(self, resource_type, instance, group=0):
        self.type = resource_type
        self.instance = instance
        self.group = group

    def __repr__(self):
        return "{:08x}:{:08x}:{:016x}".format(self.type, self.group, self.instance)


# The key (type, instance, group) to the bytes of the resource, or to an exception that load raises.
resources = {}
# Set an exception here to make list raise it.
list_error = None


def list(type=None, group=None):  # noqa: A001 - the name of the game function.
    if list_error is not None:
        raise list_error
    return [Key(key[0], key[1], key[2]) for key in resources
            if (type is None or key[0] == type) and (group is None or key[2] == group)]


def load(key):
    value = resources[(key.type, key.instance, key.group)]
    if isinstance(value, Exception):
        raise value
    return value
