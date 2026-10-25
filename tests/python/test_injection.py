# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the injection of the interaction into the object classes."""

import unittest

import support
import services
import sims4.resources
from ts4_youtube_tv import injection
from ts4_youtube_tv import interaction


class Components:
    def __init__(self, video):
        self.Video = object() if video else None
        self.state = None


def make_class(name, video, affordances=()):
    return type(name, (), {"_components": Components(video), "_super_affordances": tuple(affordances)})


class FakeInstanceManager:
    def __init__(self, types=None, instances=None):
        self.types = types or {}
        self._instances = instances or {}
        self.callbacks = []

    def get(self, instance_id):
        return self._instances.get(instance_id)

    def add_on_load_complete(self, callback):
        self.callbacks.append(callback)

    def complete(self):
        for callback in self.callbacks:
            callback(self)


class InjectionTest(unittest.TestCase):
    def setUp(self):
        support.reset_game()
        injection.reset()
        self.other = object()
        self.tv = make_class("object_tv", True, (self.other,))
        self.computer = make_class("object_computer", True)
        self.chair = make_class("object_chair", False)
        self.no_components = type("object_plain", (), {"_super_affordances": ()})
        self.objects = FakeInstanceManager(types={1: self.tv, 2: self.computer, 3: self.chair,
                                                  4: self.no_components})
        self.affordance = interaction.WatchYoutubeVideoInteraction
        self.interactions = FakeInstanceManager(instances={interaction.INSTANCE_ID: self.affordance})
        services.instance_managers[sims4.resources.Types.OBJECT] = self.objects
        services.instance_managers[sims4.resources.Types.INTERACTION] = self.interactions

    def test_adds_the_interaction_only_to_classes_with_video(self):
        self.assertTrue(injection.install())
        self.objects.complete()
        self.assertEqual(self.tv._super_affordances, (self.other, self.affordance))
        self.assertEqual(self.computer._super_affordances, (self.affordance,))
        self.assertEqual(self.chair._super_affordances, ())
        self.assertEqual(self.no_components._super_affordances, ())

    def test_is_idempotent(self):
        injection.install()
        injection.install()
        self.assertEqual(len(self.objects.callbacks), 1)
        self.objects.complete()
        self.objects.complete()
        injection.on_object_load_complete(self.objects)
        self.assertEqual(self.tv._super_affordances, (self.other, self.affordance))

    def test_does_nothing_without_the_tuning(self):
        services.instance_managers[sims4.resources.Types.INTERACTION] = FakeInstanceManager()
        injection.install()
        self.objects.complete()
        self.assertEqual(self.tv._super_affordances, (self.other,))

    def test_an_error_does_not_stop_the_game(self):
        class BadManager:
            @property
            def types(self):
                raise RuntimeError("not loaded")
        injection.on_object_load_complete(BadManager())
        services.instance_managers.clear()
        self.assertFalse(injection.install())


if __name__ == "__main__":
    unittest.main()
