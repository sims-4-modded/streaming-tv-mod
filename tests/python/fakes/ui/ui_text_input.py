# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake ui.ui_text_input module. Each class keeps its arguments."""


class _TextInputLengthFixed:
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


class UiTextInput:
    def __init__(self, *args, sort_order, **kwargs):
        self.sort_order = sort_order
        self.__dict__.update(kwargs)
