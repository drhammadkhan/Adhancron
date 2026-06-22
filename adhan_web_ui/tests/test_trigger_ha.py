import os
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

# trigger_ha reads HA settings from env when no settings file is present.
os.environ["ADHAN_SETTINGS_FILE"] = "/nonexistent/adhan_settings.json"
os.environ["HA_TOKEN"] = "test-token"
os.environ["HA_URL"] = "http://ha.test:8123"

import requests  # noqa: E402

import trigger_ha  # noqa: E402


class FakeResponse:
    def __init__(self, status_code=200, json_data=None):
        self.status_code = status_code
        self._json = json_data or {}

    def raise_for_status(self):
        if self.status_code >= 400:
            error = requests.HTTPError(f"HTTP {self.status_code}")
            error.response = self
            raise error

    def json(self):
        return self._json


class FakeHA:
    """Records service calls and replays a scripted sequence of player states."""

    def __init__(self, states, announce_play=None, standard_play=None):
        self.states = list(states)
        self.announce_play = announce_play or FakeResponse(200)
        self.standard_play = standard_play or FakeResponse(200)
        self.posts = []  # list of (service, payload)
        self.get_count = 0

    def post(self, url, json=None, headers=None, timeout=None):
        service = url.rsplit("/", 1)[-1]
        self.posts.append((service, json))
        if service == "play_media":
            response = self.announce_play if json.get("announce") else self.standard_play
        else:
            response = FakeResponse(200)
        response.raise_for_status()
        return response

    def get(self, url, headers=None, timeout=None):
        self.get_count += 1
        state = self.states.pop(0) if self.states else "idle"
        return FakeResponse(200, {"state": state})

    @property
    def play_calls(self):
        return [payload for service, payload in self.posts if service == "play_media"]


class TriggerHATests(unittest.TestCase):
    def setUp(self):
        # Confirm in a single poll with no real waiting.
        self._saved = (
            trigger_ha.USE_ANNOUNCE,
            trigger_ha.PLAYBACK_CONFIRM_TIMEOUT,
            trigger_ha.PLAYBACK_POLL_INTERVAL,
            trigger_ha.PLAY_ATTEMPTS,
        )
        trigger_ha.PLAYBACK_CONFIRM_TIMEOUT = 0
        trigger_ha.PLAYBACK_POLL_INTERVAL = 0
        trigger_ha.PLAY_ATTEMPTS = 2

    def tearDown(self):
        (
            trigger_ha.USE_ANNOUNCE,
            trigger_ha.PLAYBACK_CONFIRM_TIMEOUT,
            trigger_ha.PLAYBACK_POLL_INTERVAL,
            trigger_ha.PLAY_ATTEMPTS,
        ) = self._saved

    def _run(self, fake):
        with mock.patch.object(trigger_ha.requests, "post", fake.post), \
             mock.patch.object(trigger_ha.requests, "get", fake.get), \
             mock.patch.object(trigger_ha.time, "sleep", lambda _s: None):
            trigger_ha.trigger("http://host/audio/adhan_final.mp3", 0.8)

    def test_announce_success_does_not_fall_back(self):
        trigger_ha.USE_ANNOUNCE = True
        fake = FakeHA(states=["playing"])
        self._run(fake)
        # Exactly one play_media, and it used announce semantics.
        self.assertEqual(len(fake.play_calls), 1)
        self.assertTrue(fake.play_calls[0].get("announce"))

    def test_announce_unsupported_falls_back_to_standard(self):
        trigger_ha.USE_ANNOUNCE = True
        fake = FakeHA(
            states=["playing"],
            announce_play=FakeResponse(400),  # integration rejects announce
        )
        self._run(fake)
        self.assertEqual(len(fake.play_calls), 2)
        self.assertTrue(fake.play_calls[0].get("announce"))
        self.assertNotIn("announce", fake.play_calls[1])

    def test_no_refire_when_already_playing(self):
        # Regression guard: the old code fired play_media twice unconditionally.
        trigger_ha.USE_ANNOUNCE = False
        fake = FakeHA(states=["playing"])
        self._run(fake)
        self.assertEqual(len(fake.play_calls), 1)

    def test_retries_only_until_state_confirms(self):
        trigger_ha.USE_ANNOUNCE = False
        # First attempt never confirms, second attempt does.
        fake = FakeHA(states=["idle", "playing"])
        self._run(fake)
        self.assertEqual(len(fake.play_calls), 2)

    def test_gives_up_after_max_attempts(self):
        trigger_ha.USE_ANNOUNCE = False
        fake = FakeHA(states=["idle", "idle"])
        self._run(fake)
        self.assertEqual(len(fake.play_calls), trigger_ha.PLAY_ATTEMPTS)


if __name__ == "__main__":
    unittest.main()
