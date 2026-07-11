from __future__ import annotations

import mimetypes
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi import Response
from fastapi.responses import FileResponse
from fastapi.responses import StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from adhan_config import APP_DIR, DATA_DIR, DEFAULT_VOLUME, OVERRIDE_FILE, PRAYER_TIMES_FILE, PUBLIC_BASE_URL, SETTINGS_FILE, STATUS_FILE, default_audio_url, trigger_command
from generate_prayer_times import generate_configured_times, location_from_values
from apply_adhan_cron import apply_updates
from trigger_ha import trigger
try:
    from .cron_manager import AdhanCronManager, CronError
    from .override_manager import OverrideManager
    from .settings_manager import SettingsManager
except ImportError:
    from cron_manager import AdhanCronManager, CronError
    from override_manager import OverrideManager
    from settings_manager import SettingsManager

BASE_DIR = Path(__file__).resolve().parent
AUDIO_DIR = APP_DIR
VERSION_FILE = APP_DIR / "VERSION"

app = FastAPI(title="Adhan Cron Manager")
app.mount("/static", StaticFiles(directory=BASE_DIR / "static"), name="static")

manager = AdhanCronManager()
overrides = OverrideManager(OVERRIDE_FILE)
settings = SettingsManager(SETTINGS_FILE)


class JobUpdate(BaseModel):
    id: str
    enabled: bool
    time: str
    manual_override: bool | None = None


class UpdateRequest(BaseModel):
    jobs: list[JobUpdate]


class SettingsUpdate(BaseModel):
    ha_token: str | None = None
    ha_url: str | None = None
    ha_entity_id: str | None = None
    public_base_url: str | None = None
    latitude: str | None = None
    longitude: str | None = None


def _app_version() -> str:
    try:
        return VERSION_FILE.read_text(encoding="utf-8").strip()
    except FileNotFoundError:
        return "unknown"


@app.get("/api/version")
def version() -> dict:
    return {
        "app": "Adhancron",
        "version": _app_version(),
        "features": {
            "full_cron_command_normalization": True,
            "audio_stream_completion_logging": True,
            "cast_friendly_mp3": True,
            "announce_playback": True,
            "state_confirmed_playback": True,
            "saved_home_assistant_token": True,
            "local_location_based_timings": True,
        },
    }


def _settings_response() -> dict:
    import os

    current = settings.get_settings()
    saved_token = bool(current.get("ha_token"))
    env_token = bool(os.getenv("HA_TOKEN"))
    return {
        "ha_token_set": saved_token or env_token,
        "ha_token_source": "saved" if saved_token else "environment" if env_token else "missing",
        "settings_file": str(SETTINGS_FILE),
        "settings_file_exists": SETTINGS_FILE.exists(),
        "ha_url": current.get("ha_url") or os.getenv("HA_URL", "http://homeassistant.local:8123"),
        "ha_entity_id": current.get("ha_entity_id") or os.getenv("HA_ENTITY_ID", "media_player.bedroom_speaker"),
        "public_base_url": current.get("public_base_url") or PUBLIC_BASE_URL or "",
        "latitude": current.get("latitude") or os.getenv("ADHAN_LATITUDE", ""),
        "longitude": current.get("longitude") or os.getenv("ADHAN_LONGITUDE", ""),
        "timezone": os.getenv("TZ", "UTC"),
        "audio_url": default_audio_url(),
    }


@app.get("/api/settings")
def get_settings() -> dict:
    return _settings_response()


@app.put("/api/settings")
def update_settings(request: SettingsUpdate) -> dict:
    if request.latitude is not None or request.longitude is not None:
        current = settings.get_settings()
        latitude = request.latitude if request.latitude is not None else current.get("latitude", "")
        longitude = request.longitude if request.longitude is not None else current.get("longitude", "")
        try:
            location_from_values(latitude.strip(), longitude.strip())
        except ValueError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
    settings.update_settings(request.model_dump())
    location_updated = request.latitude is not None or request.longitude is not None
    if location_updated:
        try:
            generation = generate_configured_times()
            apply_updates()
        except ValueError as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
    try:
        audio_url = default_audio_url()
        manager.update_all_job_commands(
            audio_url,
            DEFAULT_VOLUME,
            trigger_command(audio_url, DEFAULT_VOLUME),
        )
    except CronError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    response = _settings_response()
    if location_updated:
        response["prayer_times_message"] = generation.summary
    return response


@app.post("/api/play")
def play_adhan() -> dict:
    audio_url = default_audio_url()

    try:
        trigger(audio_url, float(DEFAULT_VOLUME))
        return {"status": "playing", "audio_url": audio_url}
    except Exception as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc


@app.get("/api/prayer-times")
def get_prayer_times() -> dict:
    import json
    if not PRAYER_TIMES_FILE.exists():
        return {"error": "prayer_times.json not found"}
        
    try:
        with open(PRAYER_TIMES_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc


@app.get("/api/fajr-status")
def fajr_status() -> dict:
    import json

    if not STATUS_FILE.exists():
        return {
            "ok": False,
            "result": "unknown",
            "lastRun": None,
            "summary": "No local status file yet."
        }

    try:
        with open(STATUS_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc


@app.get("/")
@app.get("/adhan")
def index() -> FileResponse:
    return FileResponse(BASE_DIR / "static" / "index.html")


def _parse_range_header(range_header: str, file_size: int) -> tuple[int, int]:
    try:
        units, byte_range = range_header.split("=", 1)
        if units.strip().lower() != "bytes" or "," in byte_range:
            raise ValueError
        start_text, end_text = byte_range.strip().split("-", 1)

        if start_text:
            start = int(start_text)
            end = int(end_text) if end_text else file_size - 1
        else:
            suffix_length = int(end_text)
            if suffix_length <= 0:
                raise ValueError
            start = max(file_size - suffix_length, 0)
            end = file_size - 1

        if start < 0 or end >= file_size or start > end:
            raise ValueError
        return start, end
    except (TypeError, ValueError) as exc:
        raise HTTPException(
            status_code=416,
            detail="Invalid range",
            headers={"Content-Range": f"bytes */{file_size}"},
        ) from exc


def _log_audio_request(
    request: Request,
    filename: str,
    status_code: int,
    content_length: int,
    range_header: str | None,
    content_range: str | None = None,
    event: str = "start",
    bytes_sent: int = 0,
    completed: bool | None = None,
) -> None:
    import json
    from datetime import datetime

    DATA_DIR.mkdir(parents=True, exist_ok=True)
    payload = {
        "time": datetime.now().isoformat(timespec="seconds"),
        "method": request.method,
        "filename": filename,
        "status": status_code,
        "content_length": content_length,
        "range": range_header or "",
        "content_range": content_range or "",
        "client": request.client.host if request.client else "",
        "user_agent": request.headers.get("user-agent", ""),
        "event": event,
        "bytes_sent": bytes_sent,
    }
    if completed is not None:
        payload["completed"] = completed
    with open(DATA_DIR / "adhan_access.log", "a", encoding="utf-8") as f:
        f.write(json.dumps(payload) + "\n")


@app.get("/audio/{filename}")
@app.head("/audio/{filename}")
def audio(filename: str, request: Request):
    path = (AUDIO_DIR / filename).resolve()
    if path.parent != AUDIO_DIR or path.suffix.lower() != ".mp3" or not path.exists():
        raise HTTPException(status_code=404, detail="Audio file not found")

    file_size = path.stat().st_size
    range_header = request.headers.get("range")
    media_type = mimetypes.guess_type(path.name)[0] or "audio/mpeg"

    def iter_file(start: int, end: int, status_code: int, content_range: str | None = None):
        remaining = end - start + 1
        total_bytes = remaining
        bytes_sent = 0
        try:
            with open(path, "rb") as f:
                f.seek(start)
                while remaining > 0:
                    chunk = f.read(min(64 * 1024, remaining))
                    if not chunk:
                        break
                    remaining -= len(chunk)
                    bytes_sent += len(chunk)
                    yield chunk
        finally:
            _log_audio_request(
                request,
                filename,
                status_code,
                total_bytes,
                range_header,
                content_range,
                event="finish",
                bytes_sent=bytes_sent,
                completed=bytes_sent == total_bytes,
            )

    common_headers = {
        "Accept-Ranges": "bytes",
        "Cache-Control": "public, max-age=3600, no-transform",
        "Content-Disposition": f'inline; filename="{path.name}"',
    }

    if range_header:
        start, end = _parse_range_header(range_header, file_size)
        content_range = f"bytes {start}-{end}/{file_size}"
        content_length = end - start + 1
        headers = {
            **common_headers,
            "Accept-Ranges": "bytes",
            "Content-Range": content_range,
            "Content-Length": str(content_length),
        }
        _log_audio_request(request, filename, 206, content_length, range_header, content_range)
        if request.method == "HEAD":
            return Response(status_code=206, media_type=media_type, headers=headers)
        return StreamingResponse(
            iter_file(start, end, 206, content_range),
            status_code=206,
            media_type=media_type,
            headers=headers,
        )

    headers = {
        **common_headers,
        "Content-Length": str(file_size),
    }
    _log_audio_request(request, filename, 200, file_size, range_header)
    if request.method == "HEAD":
        return Response(media_type=media_type, headers=headers)
    return StreamingResponse(
        iter_file(0, file_size - 1, 200),
        media_type=media_type,
        headers=headers,
    )


@app.get("/api/jobs")
def list_jobs() -> dict:
    try:
        jobs = manager.list_jobs()
    except CronError as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc

    current_overrides = overrides.get_overrides()
    return {
        "jobs": [
            {
                "id": job.job_id,
                "label": job.label,
                "enabled": job.enabled,
                "time": job.time,
                "audio_url": job.audio_url,
                "volume": job.volume,
                "manual_override": current_overrides.get(job.label, False) if job.label else False,
            }
            for job in jobs
        ]
    }


@app.put("/api/jobs")
def update_jobs(request: UpdateRequest) -> dict:
    try:
        # 1. Map labels to existing IDs BEFORE updating crontab
        # (IDs change when time/enabled state changes)
        all_jobs_before = manager.list_jobs()
        id_to_label = {j.job_id: j.label for j in all_jobs_before if j.label}
        
        # 2. Perform the crontab update. Also refresh the trigger command so
        # manual time changes cannot preserve a stale audio URL.
        jobs = manager.update_jobs(
            [
                {
                    **job.model_dump(),
                    "audio_url": default_audio_url(),
                    "volume": DEFAULT_VOLUME,
                    "command": trigger_command(default_audio_url(), DEFAULT_VOLUME),
                }
                for job in request.jobs
            ]
        )
        
        # 3. Apply overrides using the label captured before the ID changed
        for update in request.jobs:
            if update.manual_override is not None:
                label = id_to_label.get(update.id)
                if label:
                    overrides.set_override(label, update.manual_override)
                    
    except CronError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    current_overrides = overrides.get_overrides()
    return {
        "jobs": [
            {
                "id": job.job_id,
                "label": job.label,
                "enabled": job.enabled,
                "time": job.time,
                "audio_url": job.audio_url,
                "volume": job.volume,
                "manual_override": current_overrides.get(job.label, False) if job.label else False,
            }
            for job in jobs
        ]
    }
