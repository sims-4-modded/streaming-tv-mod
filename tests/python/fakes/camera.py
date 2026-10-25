# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake camera module. It records each call."""

calls = []
_camera_position = None
_target_position = None


def walls_up_override(walls_up=True, lot_id=None):
    calls.append(("walls_up", walls_up))


def set_to_default():
    calls.append(("default",))


def focus_on_object_from_position(obj_position=None, camera_position=None, client=None):
    calls.append(("focus", obj_position, camera_position))
