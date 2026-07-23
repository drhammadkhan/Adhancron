from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from adhan_web_ui.settings_manager import SettingsManager


class SettingsManagerTests(unittest.TestCase):
    def test_large_display_settings_are_persisted(self):
        with tempfile.TemporaryDirectory() as directory:
            manager = SettingsManager(Path(directory) / "settings.json")
            saved = manager.update_settings({
                "location_name": "Kingston upon Thames",
                "display_style": "focus",
                "local_audio_device": "default",
            })
        self.assertEqual(saved["location_name"], "Kingston upon Thames")
        self.assertEqual(saved["display_style"], "focus")

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

    def test_network_speaker_settings_can_be_saved_and_cleared(self):
        with tempfile.TemporaryDirectory() as directory:
            manager = SettingsManager(Path(directory) / "settings.json")
            saved = manager.update_settings({
                "playback_method": "dlna",
                "dlna_location": "http://192.168.1.40:1400/xml/device_description.xml",
                "dlna_device_name": "Living Room",
                "airplay_identifier": "airplay-id",
            })
            cleared = manager.update_settings({
                "dlna_location": "",
                "dlna_device_name": "",
            })
        self.assertEqual(saved["playback_method"], "dlna")
        self.assertEqual(saved["dlna_device_name"], "Living Room")
        self.assertNotIn("dlna_location", cleared)
        self.assertNotIn("dlna_device_name", cleared)
        self.assertEqual(cleared["airplay_identifier"], "airplay-id")


if __name__ == "__main__":
    unittest.main()
