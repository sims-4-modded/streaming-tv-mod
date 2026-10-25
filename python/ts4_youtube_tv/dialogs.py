# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The dialogs of the mod: the text dialog for the URL (OK and Cancel), and the notification for an error.

The mod makes each dialog from the factory default of the game class, and gives each text as raw text. Thus the mod
needs no string table for the dialogs. The text box accepts 200 characters, and the game does not check it for
profanity, because a URL is not a name.
"""

from . import log

URL_INPUT_NAME = "url"
URL_MAX_LENGTH = 200

DIALOG_TITLE = "Watch a YouTube video"
DIALOG_TEXT = "Type or paste the URL of a YouTube video."
ERROR_TITLE = "YouTube TV"


def raw_text(text):
    """Return a factory of a localized string that shows the text as it is."""
    from sims4.localization import LocalizationHelperTuning

    def factory(*tokens):
        return LocalizationHelperTuning.get_raw_text(text)
    return factory


class _TextInputs:
    """The text inputs of the URL dialog, with the interface of the tuned tuple: an attribute and items()."""

    def __init__(self, inputs):
        self._inputs = dict(inputs)
        for name, value in self._inputs.items():
            setattr(self, name, value)

    def items(self):
        return self._inputs.items()

    def values(self):
        return self._inputs.values()

    def __iter__(self):
        return iter(self._inputs)


def make_url_text_input():
    """Return the text input for the URL: 200 characters, no profanity check, no title."""
    from ui.ui_text_input import UiTextInput, _TextInputLengthFixed
    length = _TextInputLengthFixed(min_length=None, max_length=URL_MAX_LENGTH)
    return UiTextInput(sort_order=0, default_text=None, initial_value=None, title=None,
                       length_restriction=length, restricted_characters=None, check_profanity=False,
                       height=None)


def make_url_dialog(owner, resolver=None):
    """Return the URL dialog for the Sim owner."""
    from ui.ui_dialog_generic import UiDialogTextInputOkCancel
    factory = UiDialogTextInputOkCancel.TunableFactory(text_inputs=(URL_INPUT_NAME,)).default
    text_inputs = _TextInputs({URL_INPUT_NAME: make_url_text_input()})
    return factory(owner, resolver=resolver, title=raw_text(DIALOG_TITLE), text=raw_text(DIALOG_TEXT),
                   text_inputs=text_inputs)


def show_url_dialog(owner, on_url, resolver=None):
    """Show the URL dialog. On OK, call on_url(text). On Cancel, do nothing."""
    dialog = make_url_dialog(owner, resolver)

    def on_response(response_dialog):
        try:
            if not response_dialog.accepted:
                log.write("dialog: the player cancelled the URL dialog")
                return
            text = response_dialog.text_input_responses.get(URL_INPUT_NAME, "")
            on_url(text or "")
        except Exception:  # noqa: BLE001
            log.exception("dialog: the response of the URL dialog failed")

    dialog.show_dialog(on_response=on_response)
    return dialog


def show_error(owner, message):
    """Show a notification with the error text."""
    try:
        from ui.ui_dialog_notification import UiDialogNotification
        factory = UiDialogNotification.TunableFactory().default
        notification = factory(owner, title=raw_text(ERROR_TITLE), text=raw_text(message))
        notification.show_dialog()
    except Exception:  # noqa: BLE001
        log.exception("dialog: the notification failed: {}".format(message))
