from __future__ import annotations

import unittest
from unittest import mock

import update_adhan


class UpdateAdhanTests(unittest.TestCase):
    def test_first_run_without_location_does_not_stop_the_container(self):
        with mock.patch.object(
            update_adhan,
            "ensure_configured_times",
            side_effect=ValueError("Set latitude and longitude in the web UI before generating prayer times"),
        ), mock.patch.object(update_adhan, "apply_updates") as apply_updates:
            self.assertEqual(update_adhan.run(), 0)
        apply_updates.assert_not_called()
