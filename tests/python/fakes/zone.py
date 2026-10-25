# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake zone module. Zone.on_teardown records each call."""


class Zone:
    teardown_calls = []

    def on_teardown(self, client):
        Zone.teardown_calls.append(client)
