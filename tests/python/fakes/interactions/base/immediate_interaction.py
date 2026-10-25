# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake ImmediateSuperInteraction."""

from event_testing.results import TestResult


class ImmediateSuperInteraction:
    base_test_calls = []

    def __init__(self, target, sim):
        self.target = target
        self.sim = sim

    @classmethod
    def _test(cls, target, context, **interaction_parameters):
        ImmediateSuperInteraction.base_test_calls.append(target)
        return TestResult(True)

    def get_resolver(self):
        return ("resolver", self)
