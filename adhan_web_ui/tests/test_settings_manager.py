from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from adhan_web_ui.settings_manager import SettingsManager


class SettingsManagerTests(unittest.TestCase):
    def test_eid_dates_can_be_added_and_cleared_without_losing_other_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            manager = SettingsManager(Path(directory) / "settings.json")
            manager.update_settings({
                "ha_url": "http://homeassistant.test:8123",
                "eid_fitr_date": "2027-03-10",
                "eid_takbeer_start": "07:00",
            })
            updated = manager.update_settings({"eid_fitr_date": ""})
        self.assertNotIn("eid_fitr_date", updated)
        self.assertEqual(updated["ha_url"], "http://homeassistant.test:8123")
        self.assertEqual(updated["eid_takbeer_start"], "07:00")


if __name__ == "__main__":
    unittest.main()
