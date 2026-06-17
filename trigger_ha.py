from __future__ import annotations

import json
import os
import sys
import time
from datetime import datetime

import requests

from adhan_config import SETTINGS_FILE


DEFAULT_HA_BASE_URL = "http://homeassistant.local:8123"
REQUEST_TIMEOUT = float(os.getenv("HA_REQUEST_TIMEOUT", "15"))
PLAY_ATTEMPTS = int(os.getenv("ADHAN_PLAY_ATTEMPTS", "2"))
PLAY_RETRY_DELAY = float(os.getenv("ADHAN_PLAY_RETRY_DELAY", "4"))
PRE_PLAY_DELAY = float(os.getenv("ADHAN_PRE_PLAY_DELAY", "1"))
MEDIA_CONTENT_TYPE = os.getenv("ADHAN_MEDIA_CONTENT_TYPE", "audio/mpeg")
STOP_BEFORE_PLAY = os.getenv("ADHAN_STOP_BEFORE_PLAY", "true").strip().lower() in {"1", "true", "yes", "on"}


def _load_settings() -> dict:
    settings_path = os.getenv("ADHAN_SETTINGS_FILE", str(SETTINGS_FILE))
    try:
        with open(settings_path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def _log(message: str) -> None:
    timestamp = datetime.now().isoformat(timespec="seconds")
    print(f"[{timestamp}] {message}", flush=True)


def _setting(name: str, env_name: str, default: str = "") -> str:
    settings = _load_settings()
    value = settings.get(name)
    if isinstance(value, str) and value.strip():
        return value.strip()
    return os.getenv(env_name, default)


def _setting_source(name: str, env_name: str) -> str:
    settings = _load_settings()
    value = settings.get(name)
    if isinstance(value, str) and value.strip():
        return "saved"
    if os.getenv(env_name):
        return "environment"
    return "default"


def _load_ha_token() -> str:
    token = _setting("ha_token", "HA_TOKEN")
    if token:
        return token

    config_path = os.getenv(
        "HA_CONFIG_FILE",
        os.path.expanduser("~/.config/home-assistant/config.json"),
    )
    try:
        with open(config_path, "r", encoding="utf-8") as f:
            return json.load(f)["token"]
    except Exception as exc:
        raise RuntimeError("Set HA_TOKEN or configure HA_CONFIG_FILE") from exc


def _service_url(service: str) -> str:
    configured = _ha_base_url()
    if "/api/services/" in configured:
        base = configured.split("/api/services/", 1)[0]
    else:
        base = configured
    return f"{base}/api/services/media_player/{service}"


def _ha_base_url() -> str:
    return _setting("ha_url", "HA_URL", DEFAULT_HA_BASE_URL).rstrip("/")


def _token_source() -> str:
    if _setting("ha_token", "HA_TOKEN"):
        return _setting_source("ha_token", "HA_TOKEN")
    return "missing"


def _post_service(service: str, payload: dict) -> requests.Response:
    headers = {
        "Authorization": f"Bearer {_load_ha_token()}",
        "Content-Type": "application/json",
    }
    response = requests.post(
        _service_url(service),
        json=payload,
        headers=headers,
        timeout=REQUEST_TIMEOUT,
    )
    response.raise_for_status()
    return response


def _try_post_service(service: str, payload: dict) -> requests.Response | None:
    try:
        return _post_service(service, payload)
    except requests.HTTPError as exc:
        status_code = exc.response.status_code if exc.response is not None else "unknown"
        _log(f"Ignoring {service} failure before playback: HTTP {status_code}: {exc}")
    except requests.RequestException as exc:
        _log(f"Ignoring {service} network failure before playback: {exc}")
    return None


def trigger(media_url: str, volume: float = 0.8) -> None:
    entity_id = _setting("ha_entity_id", "HA_ENTITY_ID", "media_player.bedroom_speaker")
    attempts = max(1, PLAY_ATTEMPTS)
    _log(
        "Trigger start: "
        f"media_url={media_url}, volume={volume}, "
        f"ha_url={_ha_base_url()}, "
        f"entity_id={entity_id}, "
        f"token_source={_token_source()}, "
        f"settings_file={os.getenv('ADHAN_SETTINGS_FILE', str(SETTINGS_FILE))}, "
        f"attempts={attempts}, retry_delay={PLAY_RETRY_DELAY}, "
        f"media_content_type={MEDIA_CONTENT_TYPE}, stop_before_play={STOP_BEFORE_PLAY}"
    )
    if STOP_BEFORE_PLAY:
        _try_post_service("media_stop", {"entity_id": entity_id})
        if PRE_PLAY_DELAY > 0:
            time.sleep(PRE_PLAY_DELAY)

    _post_service(
        "volume_set",
        {"entity_id": entity_id, "volume_level": volume},
    )
    response = None
    for attempt in range(1, attempts + 1):
        response = _post_service(
            "play_media",
            {
                "entity_id": entity_id,
                "media_content_id": media_url,
                "media_content_type": MEDIA_CONTENT_TYPE,
            },
        )
        _log(
            f"Play media attempt {attempt}/{attempts}: "
            f"media_url={media_url}, status={response.status_code}"
        )
        if attempt < attempts and PLAY_RETRY_DELAY > 0:
            time.sleep(PLAY_RETRY_DELAY)
    _log(f"Trigger complete: media_url={media_url}, status={response.status_code if response else 'unknown'}")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("Usage: python3 trigger_ha.py <media_url> [volume]", file=sys.stderr)
        return 2

    volume = float(argv[2]) if len(argv) > 2 else 0.8
    trigger(argv[1], volume)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv))
    except Exception as exc:
        _log(f"Trigger failed: {type(exc).__name__}: {exc}")
        raise
