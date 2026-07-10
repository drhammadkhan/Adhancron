from __future__ import annotations

import unittest

from generate_prayer_times import _format_month, location_from_values


class PrayerTimeGeneratorTests(unittest.TestCase):
    def test_location_validation(self):
        location = location_from_values("51.5074", "-0.1278")
        self.assertEqual(location.latitude, 51.5074)
        self.assertEqual(location.longitude, -0.1278)
        with self.assertRaises(ValueError):
            location_from_values("91", "0")

    def test_al_islam_response_is_written_in_existing_json_shape(self):
        payload = {
            "locationInfo": {"timezone": "Europe/London"},
            "multiDayTimings": [
                {
                    "date": 1782937121011,
                    "prayers": [
                        {"name": "Fajr", "time": 1782872280000},
                        {"name": "Sunrise", "time": 1782877680000},
                        {"name": "Zuhr", "time": 1782907740000},
                        {"name": "Asr", "time": 1782923160000},
                        {"name": "Sunset", "time": 1782937260000},
                        {"name": "Maghrib", "time": 1782937320000},
                        {"name": "Isha", "time": 1782941940000},
                    ],
                }
            ],
        }
        data, timezone = _format_month(payload)
        self.assertEqual(timezone, "Europe/London")
        self.assertEqual(
            data["2026"]["July"]["1"],
            {"Fajr": "03:18", "Dhuhr": "13:09", "Asr": "17:26", "Maghrib": "21:22", "Isha": "22:39"},
        )
