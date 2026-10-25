# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake UiDialogNotification that keeps each shown notification."""

shown = []


class FakeNotification:
    def __init__(self, owner, **kwargs):
        self.owner = owner
        self.kwargs = kwargs

    def show_dialog(self, **kwargs):
        shown.append(self)


class _Factory:
    def default(self, owner, **kwargs):
        return FakeNotification(owner, **kwargs)


class UiDialogNotification:
    @classmethod
    def TunableFactory(cls, **kwargs):
        return _Factory()
