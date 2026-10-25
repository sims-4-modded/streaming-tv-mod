# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake sims4.localization package."""


class RawText:
    def __init__(self, text):
        self.text = text

    def __eq__(self, other):
        return isinstance(other, RawText) and other.text == self.text

    def __repr__(self):
        return "RawText({!r})".format(self.text)


class LocalizationHelperTuning:
    @classmethod
    def get_raw_text(cls, text):
        return RawText(text)
