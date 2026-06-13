from __future__ import annotations

import json
import os
import sys

import requests

from adhan_config import SETTINGS_FILE


DEFAULT_HA_BASE_URL = "http://homeassistant.local:8123"
REQUEST_TIMEOUT = float(os.getenv("HA_REQUEST_TIMEOUT", "15"))


def _load_settings() -> dict:
    settings_path = os.getenv("ADHAN_SETTINGS_FILE", str(SETTINGS_FILE))
    try:
        with open(settings_path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def _setting(name: str, env_name: str, default: str = "") -> str:
    settings = _load_settings()
    value = settings.get(name)
    if isinstance(value, str) and value.strip():
        return value.strip()
    return os.getenv(env_name, default)


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
    configured = _setting("ha_url", "HA_URL", DEFAULT_HA_BASE_URL).rstrip("/")
    if "/api/services/" in configured:
        base = configured.split("/api/services/", 1)[0]
    else:
        base = configured
    return f"{base}/api/services/media_player/{service}"


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


def trigger(media_url: str, volume: float = 0.8) -> None:
    entity_id = _setting("ha_entity_id", "HA_ENTITY_ID", "media_player.bedroom_speaker")
    _post_service(
        "volume_set",
        {"entity_id": entity_id, "volume_level": volume},
    )
    response = _post_service(
        "play_media",
        {
            "entity_id": entity_id,
            "media_content_id": media_url,
            "media_content_type": "music",
        },
    )
    print(f"Triggered {media_url}, status: {response.status_code}")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("Usage: python3 trigger_ha.py <media_url> [volume]", file=sys.stderr)
        return 2

    volume = float(argv[2]) if len(argv) > 2 else 0.8
    trigger(argv[1], volume)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
