from __future__ import annotations

import os
import unittest
from datetime import date

from generate_prayer_times import _isha_offset_seconds, calculate_day, location_from_values


class PrayerTimeGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.previous_timezone = os.environ.get("TZ")
        os.environ["TZ"] = "Europe/London"

    def tearDown(self):
        if self.previous_timezone is None:
            os.environ.pop("TZ", None)
        else:
            os.environ["TZ"] = self.previous_timezone

    def test_location_validation(self):
        location = location_from_values("51.5074", "-0.1278")
        self.assertEqual(location.latitude, 51.5074)
        self.assertEqual(location.longitude, -0.1278)
        with self.assertRaises(ValueError):
            location_from_values("91", "0")

    def test_london_summer_times_match_the_alislam_calendar(self):
        location = location_from_values("51.5074", "-0.1278")
        times = calculate_day(date(2026, 7, 1), location)
        self.assertEqual(
            {name: times[name] for name in ("Fajr", "Dhuhr", "Asr", "Maghrib")},
            {"Fajr": "03:18", "Dhuhr": "13:09", "Asr": "17:26", "Maghrib": "21:22"},
        )
        # Astral and Alislam use different solar-position engines; both place
        # this seasonal Isha value within a minute of 22:39.
        self.assertIn(times["Isha"], {"22:38", "22:39"})

    def test_seasonal_isha_adjustment_matches_london_samples(self):
        self.assertEqual(_isha_offset_seconds(date(2026, 1, 1), 51.5074), 5_779)
        self.assertEqual(_isha_offset_seconds(date(2026, 5, 1), 51.5074), 4_065)
        self.assertEqual(_isha_offset_seconds(date(2026, 6, 22), 51.5074), 4_845)
        self.assertEqual(_isha_offset_seconds(date(2026, 12, 22), 51.5074), 5_924)
