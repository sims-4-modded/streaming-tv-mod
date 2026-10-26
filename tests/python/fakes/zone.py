# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake zone module. Zone.start_services and Zone.on_teardown record each call."""


class Zone:
    start_calls = []
    teardown_calls = []

    def start_services(self, gameplay_zone_data, save_slot_data):
        Zone.start_calls.append((gameplay_zone_data, save_slot_data))

    def on_teardown(self, client):
        Zone.teardown_calls.append(client)
