# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake UiDialogTextInputOkCancel. The tests give the response of the player."""

OK = 1
CANCEL = 2

shown = []


class FakeTextDialog:
    def __init__(self, owner, **kwargs):
        self.owner = owner
        self.kwargs = kwargs
        self.response = None
        self.text_input_responses = {}
        self._listener = None

    @property
    def accepted(self):
        return self.response == OK

    def show_dialog(self, on_response=None, **kwargs):
        self._listener = on_response
        shown.append(self)

    def respond(self, response, text=None):
        self.response = response
        if text is not None:
            self.text_input_responses["url"] = text
        self._listener(self)


class _Factory:
    def __init__(self, text_inputs):
        self.text_inputs = text_inputs

    def default(self, owner, **kwargs):
        return FakeTextDialog(owner, **kwargs)


class UiDialogTextInputOkCancel:
    @classmethod
    def TunableFactory(cls, text_inputs=(), **kwargs):
        return _Factory(text_inputs)
