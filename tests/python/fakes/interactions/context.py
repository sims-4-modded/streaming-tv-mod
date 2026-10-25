# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake interactions.context."""


class InteractionContext:
    SOURCE_PIE_MENU = "pie_menu"
    SOURCE_SCRIPT = "script"
    SOURCE_SCRIPT_WITH_USER_INTENT = "script_with_user_intent"

    def __init__(self, sim, source, priority, **kwargs):
        self.sim = sim
        self.source = source
        self.priority = priority
