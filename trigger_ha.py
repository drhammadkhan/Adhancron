from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid
from datetime import datetime
from urllib.parse import unquote, urlparse

import requests

from adhan_config import SETTINGS_FILE


DEFAULT_HA_BASE_URL = "http://homeassistant.local:8123"
REQUEST_TIMEOUT = float(os.getenv("HA_REQUEST_TIMEOUT", "15"))
CAST_CONNECT_TIMEOUT = float(os.getenv("ADHAN_CAST_CONNECT_TIMEOUT", "30"))
MEDIA_CONTENT_TYPE = os.getenv("ADHAN_MEDIA_CONTENT_TYPE", "audio/mpeg")


def _is_truthy(value: str) -> bool:
    return value.strip().lower() in {"1", "true", "yes", "on"}


# Try Home Assistant's announcement semantics first. `announce: true` is the
# idiomatic "play this sound now" call: it wakes the device, plays once, and
# restores prior state, which is what we want for an adhan. Not every
# integration supports it, so we fall back to a plain play_media below.
USE_ANNOUNCE = _is_truthy(os.getenv("ADHAN_USE_ANNOUNCE", "true"))

# Closed-loop playback confirmation. After asking Home Assistant to play, we
# poll the media_player state until it actually reports playing rather than
# blindly firing the command again. We only re-send play_media if the device
# never reached a playing state, which avoids interrupting playback that has
# already started (the old unconditional double-fire could restart Cast media
# mid-stream and cause the "started then cut out" symptom).
PLAY_ATTEMPTS = max(1, int(os.getenv("ADHAN_PLAY_ATTEMPTS", "2")))
PLAYBACK_CONFIRM_TIMEOUT = float(os.getenv("ADHAN_PLAYBACK_TIMEOUT", "15"))
PLAYBACK_POLL_INTERVAL = float(os.getenv("ADHAN_PLAYBACK_POLL_INTERVAL", "1"))

PLAYING_STATES = {"playing", "buffering"}
PLAYBACK_METHOD_HOME_ASSISTANT = "home_assistant"
PLAYBACK_METHOD_GOOGLE_CAST = "google_cast"
PLAYBACK_METHOD_AIRPLAY = "airplay"
PLAYBACK_METHOD_DLNA = "dlna"
PLAYBACK_METHOD_ATTACHED = "attached"


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


def _api_root() -> str:
    configured = _ha_base_url()
    if "/api/services/" in configured:
        return configured.split("/api/services/", 1)[0]
    return configured


def _service_url(service: str) -> str:
    return f"{_api_root()}/api/services/media_player/{service}"


def _state_url(entity_id: str) -> str:
    return f"{_api_root()}/api/states/{entity_id}"


def _ha_base_url() -> str:
    return _setting("ha_url", "HA_URL", DEFAULT_HA_BASE_URL).rstrip("/")


def _token_source() -> str:
    if _setting("ha_token", "HA_TOKEN"):
        return _setting_source("ha_token", "HA_TOKEN")
    return "missing"


def _playback_method() -> str:
    method = _setting("playback_method", "ADHAN_PLAYBACK_METHOD", PLAYBACK_METHOD_HOME_ASSISTANT)
    return method.strip().lower()


def _google_cast_host() -> str:
    return _setting("google_cast_host", "GOOGLE_CAST_HOST")


def _google_cast_port() -> int:
    raw_port = _setting("google_cast_port", "GOOGLE_CAST_PORT", "8009")
    try:
        port = int(raw_port)
    except ValueError as exc:
        raise RuntimeError("Google Cast port must be a number") from exc
    if not 1 <= port <= 65535:
        raise RuntimeError("Google Cast port must be between 1 and 65535")
    return port


def _airplay_identifier() -> str:
    return _setting("airplay_identifier", "AIRPLAY_IDENTIFIER")


def _dlna_location() -> str:
    return _setting("dlna_location", "DLNA_LOCATION")


def _attached_audio_path(media_url: str) -> Path:
    filename = Path(unquote(urlparse(media_url).path)).name
    if not filename or filename in {".", ".."}:
        raise RuntimeError("The attached-speaker audio file is invalid")
    app_dir = Path(os.getenv("ADHAN_APP_DIR", Path(__file__).resolve().parent))
    data_dir = Path(os.getenv("ADHAN_DATA_DIR", app_dir / "data"))
    for candidate in (data_dir / filename, app_dir / filename):
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"Audio file not found for attached playback: {filename}")


def _trigger_attached(media_url: str, volume: float) -> None:
    audio_path = _attached_audio_path(media_url)
    alsa_device = _setting("local_audio_device", "ADHAN_ALSA_DEVICE", "default")
    scale = round(max(0.0, min(1.0, volume)) * 32768)
    try:
        subprocess.Popen(
            ["mpg123", "-q", "-a", alsa_device, "-f", str(scale), str(audio_path)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except FileNotFoundError as exc:
        raise RuntimeError("Attached playback requires mpg123") from exc
    _log(
        "Trigger complete: attached speaker playback started "
        f"device={alsa_device}, file={audio_path.name}"
    )


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
        _log(f"Ignoring {service} failure: HTTP {status_code}: {exc}")
    except requests.RequestException as exc:
        _log(f"Ignoring {service} network failure: {exc}")
    return None


def _entity_state(entity_id: str) -> str | None:
    headers = {"Authorization": f"Bearer {_load_ha_token()}"}
    response = requests.get(_state_url(entity_id), headers=headers, timeout=REQUEST_TIMEOUT)
    response.raise_for_status()
    return response.json().get("state")


def _wait_until_playing(entity_id: str) -> bool:
    """Poll the media_player until it reports a playing state or we time out."""
    deadline = time.monotonic() + PLAYBACK_CONFIRM_TIMEOUT
    last_state = "unknown"
    while True:
        try:
            last_state = _entity_state(entity_id) or "unknown"
            if last_state in PLAYING_STATES:
                return True
        except requests.RequestException as exc:
            last_state = "error"
            _log(f"State poll error for {entity_id}: {exc}")
        if time.monotonic() >= deadline:
            _log(f"Playback not confirmed for {entity_id}; last state={last_state}")
            return False
        time.sleep(PLAYBACK_POLL_INTERVAL)


def _play(entity_id: str, media_url: str, announce: bool) -> requests.Response:
    payload = {
        "entity_id": entity_id,
        "media_content_id": media_url,
        "media_content_type": MEDIA_CONTENT_TYPE,
    }
    if announce:
        payload["announce"] = True
    return _post_service("play_media", payload)


def _trigger_google_cast(media_url: str, volume: float) -> None:
    host = _google_cast_host()
    if not host:
        raise RuntimeError("Set a Google Cast speaker IP address or hostname")
    port = _google_cast_port()
    try:
        import pychromecast
    except ImportError as exc:
        raise RuntimeError("PyChromecast is not installed") from exc

    cast = None
    try:
        cast = pychromecast.get_chromecast_from_host(
            (host, port, uuid.uuid4(), None, None),
            tries=1,
            timeout=CAST_CONNECT_TIMEOUT,
        )
        try:
            cast.wait(timeout=CAST_CONNECT_TIMEOUT)
        except TimeoutError as exc:
            raise RuntimeError(
                f"Could not connect to Google Cast speaker at {host}:{port} within "
                f"{CAST_CONNECT_TIMEOUT:g} seconds. Confirm this is the current IP "
                "address of an individual speaker, not a speaker group."
            ) from exc
        cast.set_volume(volume, timeout=REQUEST_TIMEOUT)
        media = cast.media_controller
        media.play_media(media_url, MEDIA_CONTENT_TYPE, stream_type="BUFFERED")
        media.block_until_active(timeout=REQUEST_TIMEOUT)
        _log(
            "Trigger complete: direct Google Cast playback active "
            f"host={host}, port={port}, media_url={media_url}"
        )
    finally:
        if cast is not None:
            cast.disconnect(timeout=1)


def trigger(media_url: str, volume: float = 0.8) -> None:
    playback_method = _playback_method()
    if playback_method == PLAYBACK_METHOD_ATTACHED:
        _log(
            "Trigger start: "
            f"method=attached, media_url={media_url}, volume={volume}"
        )
        _trigger_attached(media_url, volume)
        return
    if playback_method == PLAYBACK_METHOD_GOOGLE_CAST:
        _log(
            "Trigger start: "
            f"method=google_cast, media_url={media_url}, volume={volume}, "
            f"host={_google_cast_host() or 'missing'}, settings_file="
            f"{os.getenv('ADHAN_SETTINGS_FILE', str(SETTINGS_FILE))}"
        )
        _trigger_google_cast(media_url, volume)
        return
    if playback_method == PLAYBACK_METHOD_AIRPLAY:
        from network_speakers import play_airplay

        identifier = _airplay_identifier()
        _log(
            "Trigger start: "
            f"method=airplay, media_url={media_url}, volume={volume}, "
            f"identifier={identifier or 'missing'}"
        )
        play_airplay(identifier, media_url, volume)
        _log("Trigger complete: AirPlay playback finished")
        return
    if playback_method == PLAYBACK_METHOD_DLNA:
        from network_speakers import play_dlna

        location = _dlna_location()
        if not location:
            raise RuntimeError("Choose a DLNA or Sonos speaker")
        _log(
            "Trigger start: "
            f"method=dlna, media_url={media_url}, volume={volume}, "
            f"location={location}"
        )
        device = play_dlna(location, media_url, volume)
        _log(f"Trigger complete: DLNA playback started on {device.name}")
        return
    if playback_method != PLAYBACK_METHOD_HOME_ASSISTANT:
        raise RuntimeError(f"Unsupported playback method: {playback_method}")

    entity_id = _setting("ha_entity_id", "HA_ENTITY_ID", "media_player.bedroom_speaker")
    _log(
        "Trigger start: "
        f"method=home_assistant, media_url={media_url}, volume={volume}, "
        f"ha_url={_ha_base_url()}, "
        f"entity_id={entity_id}, "
        f"token_source={_token_source()}, "
        f"settings_file={os.getenv('ADHAN_SETTINGS_FILE', str(SETTINGS_FILE))}, "
        f"use_announce={USE_ANNOUNCE}, attempts={PLAY_ATTEMPTS}, "
        f"confirm_timeout={PLAYBACK_CONFIRM_TIMEOUT}, "
        f"media_content_type={MEDIA_CONTENT_TYPE}"
    )

    # Set the level first so the adhan plays at the configured volume. A volume
    # failure should not abort playback, so it is best-effort.
    _try_post_service("volume_set", {"entity_id": entity_id, "volume_level": volume})

    # 1. Preferred path: a single announcement, confirmed by state.
    if USE_ANNOUNCE:
        try:
            response = _play(entity_id, media_url, announce=True)
            _log(f"play_media announce sent: status={response.status_code}")
            if _wait_until_playing(entity_id):
                _log("Trigger complete: playback confirmed (announce)")
                return
            _log("Announce did not reach a playing state; falling back to standard play")
        except requests.HTTPError as exc:
            status_code = exc.response.status_code if exc.response is not None else "unknown"
            _log(f"Announce not supported (HTTP {status_code}); falling back to standard play")

    # 2. Fallback: plain play_media, re-sent only when state never confirms.
    for attempt in range(1, PLAY_ATTEMPTS + 1):
        response = _play(entity_id, media_url, announce=False)
        _log(f"play_media attempt {attempt}/{PLAY_ATTEMPTS}: status={response.status_code}")
        if _wait_until_playing(entity_id):
            _log(f"Trigger complete: playback confirmed (standard, attempt {attempt})")
            return

    _log("Trigger complete: playback could not be confirmed")


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
