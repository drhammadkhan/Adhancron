from __future__ import annotations

from datetime import date, datetime
from pathlib import Path
import tempfile
import unittest

from eid_schedule import (
    dispatch_eid_if_due,
    latest_due_slot,
    next_slot,
    status_for_date,
    validate_eid_settings,
)


SETTINGS = {
    "eid_fitr_date": "2027-03-10",
    "eid_adha_date": "2027-05-17",
    "eid_takbeer_start": "07:00",
    "eid_takbeer_end": "09:00",
    "eid_takbeer_interval": "15",
}


class EidScheduleTests(unittest.TestCase):
    def test_matches_each_configured_eid_date(self):
        fitr = status_for_date(SETTINGS, date(2027, 3, 10))
        adha = status_for_date(SETTINGS, date(2027, 5, 17))
        ordinary = status_for_date(SETTINGS, date(2027, 5, 18))
        self.assertTrue(fitr.active)
        self.assertEqual(fitr.kind, "Eid al-Fitr")
        self.assertEqual(adha.kind, "Eid al-Adha")
        self.assertFalse(ordinary.active)

    def test_schedule_includes_start_finish_and_two_minute_recovery(self):
        self.assertEqual(latest_due_slot(SETTINGS, datetime(2027, 3, 10, 7, 0)), 420)
        self.assertEqual(latest_due_slot(SETTINGS, datetime(2027, 3, 10, 7, 2)), 420)
        self.assertIsNone(latest_due_slot(SETTINGS, datetime(2027, 3, 10, 7, 3)))
        self.assertEqual(latest_due_slot(SETTINGS, datetime(2027, 3, 10, 9, 2)), 540)
        self.assertIsNone(latest_due_slot(SETTINGS, datetime(2027, 3, 10, 9, 3)))

    def test_next_slot_advances_after_current_slot_starts(self):
        self.assertEqual(next_slot(SETTINGS, datetime(2027, 3, 10, 6, 50)), datetime(2027, 3, 10, 7, 0))
        self.assertEqual(next_slot(SETTINGS, datetime(2027, 3, 10, 7, 0, 1)), datetime(2027, 3, 10, 7, 15))
        self.assertIsNone(next_slot(SETTINGS, datetime(2027, 3, 10, 9, 0, 1)))

    def test_rejects_bad_dates_windows_and_intervals(self):
        with self.assertRaisesRegex(ValueError, "YYYY-MM-DD"):
            validate_eid_settings({**SETTINGS, "eid_fitr_date": "10/03/2027"})
        with self.assertRaisesRegex(ValueError, "earlier"):
            validate_eid_settings({**SETTINGS, "eid_takbeer_start": "10:00"})
        with self.assertRaisesRegex(ValueError, "between 5 and 120"):
            validate_eid_settings({**SETTINGS, "eid_takbeer_interval": "2"})

    def test_dispatches_each_slot_once(self):
        calls: list[tuple[str, float]] = []
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory) / "state.json"
            now = datetime(2027, 3, 10, 7, 1)
            first = dispatch_eid_if_due(
                now,
                current_settings=SETTINGS,
                state_file=state,
                trigger_func=lambda url, volume: calls.append((url, volume)),
            )
            second = dispatch_eid_if_due(
                now,
                current_settings=SETTINGS,
                state_file=state,
                trigger_func=lambda url, volume: calls.append((url, volume)),
            )
        self.assertTrue(first)
        self.assertFalse(second)
        self.assertEqual(len(calls), 1)
        self.assertTrue(calls[0][0].endswith("/audio/eid_takbeer.mp3"))

    def test_failed_dispatch_releases_slot_for_recovery_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory) / "state.json"

            def fail(_url: str, _volume: float) -> None:
                raise RuntimeError("speaker unavailable")

            with self.assertRaisesRegex(RuntimeError, "speaker unavailable"):
                dispatch_eid_if_due(
                    datetime(2027, 3, 10, 7, 0),
                    current_settings=SETTINGS,
                    state_file=state,
                    trigger_func=fail,
                )
            calls: list[str] = []
            self.assertTrue(
                dispatch_eid_if_due(
                    datetime(2027, 3, 10, 7, 1),
                    current_settings=SETTINGS,
                    state_file=state,
                    trigger_func=lambda url, _volume: calls.append(url),
                )
            )
        self.assertEqual(len(calls), 1)


if __name__ == "__main__":
    unittest.main()
