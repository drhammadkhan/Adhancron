from __future__ import annotations

import os
from pathlib import Path
import shlex


APP_DIR = Path(os.getenv("ADHAN_APP_DIR", Path(__file__).resolve().parent)).resolve()
DATA_DIR = Path(os.getenv("ADHAN_DATA_DIR", APP_DIR / "data")).resolve()
ENV_FILE = DATA_DIR / "adhan.env"

PRAYER_TIMES_FILE = Path(
    os.getenv("PRAYER_TIMES_FILE", str(APP_DIR / "prayer_times.json"))
).resolve()
OVERRIDE_FILE = Path(
    os.getenv("ADHAN_OVERRIDE_FILE", str(DATA_DIR / "adhan_overrides.json"))
).resolve()
STATUS_FILE = Path(
    os.getenv("ADHAN_STATUS_FILE", str(DATA_DIR / "adhan_update_status.json"))
).resolve()
SETTINGS_FILE = Path(
    os.getenv("ADHAN_SETTINGS_FILE", str(DATA_DIR / "adhan_settings.json"))
).resolve()

PRAYER_ORDER = ["Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"]

DEFAULT_VOLUME = os.getenv("ADHAN_VOLUME", "0.8")
DEFAULT_AUDIO_FILE = os.getenv("ADHAN_AUDIO_FILE", "adhan_final.mp3")

PUBLIC_BASE_URL = os.getenv("PUBLIC_BASE_URL", "").rstrip("/")
ADHAN_AUDIO_BASE_URL = os.getenv("ADHAN_AUDIO_BASE_URL", "").rstrip("/")


def default_audio_url() -> str:
    explicit_url = os.getenv("ADHAN_AUDIO_URL")
    if explicit_url:
        return explicit_url
    if ADHAN_AUDIO_BASE_URL:
        return f"{ADHAN_AUDIO_BASE_URL}/{DEFAULT_AUDIO_FILE}"
    if PUBLIC_BASE_URL:
        return f"{PUBLIC_BASE_URL}/audio/{DEFAULT_AUDIO_FILE}"
    return f"http://127.0.0.1:8090/audio/{DEFAULT_AUDIO_FILE}"


def trigger_command(audio_url: str | None = None, volume: str | None = None) -> str:
    resolved_audio_url = audio_url or default_audio_url()
    resolved_volume = volume or DEFAULT_VOLUME
    return (
        f"cd {shlex.quote(str(APP_DIR))} && . {shlex.quote(str(ENV_FILE))} "
        f"&& /usr/local/bin/python {shlex.quote(str(APP_DIR / 'trigger_ha.py'))} "
        f"{shlex.quote(resolved_audio_url)} {shlex.quote(str(resolved_volume))} "
        f">> {shlex.quote(str(DATA_DIR / 'adhan.log'))} 2>&1"
    )
