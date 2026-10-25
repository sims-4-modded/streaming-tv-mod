# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake objects.components.state: the managed distributables of the state component."""


class StateComponentManagedDistributables:
    """Keeps one started client state change for each (attribute name, state), as the game does."""

    def __init__(self):
        self._distributables_map = {}

    def apply(self, target, attr_name, state, state_value, attr_value, immediate=False, **kwargs):
        state_owner = getattr(state, "overridden_by", None) or state
        key = (attr_name, state_owner)
        if key in self._distributables_map:
            self._distributables_map[key].stop(immediate=immediate)
            del self._distributables_map[key]
        if attr_value is not None:
            distributable = attr_value(target)
            distributable.start()
            self._distributables_map[key] = distributable
