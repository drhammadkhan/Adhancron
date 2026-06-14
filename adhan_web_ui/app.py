from __future__ import annotations

import mimetypes
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import FileResponse
from fastapi.responses import StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from adhan_config import APP_DIR, DEFAULT_VOLUME, OVERRIDE_FILE, PUBLIC_BASE_URL, SETTINGS_FILE, STATUS_FILE, default_audio_url
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
        "audio_url": default_audio_url(),
    }


@app.get("/api/settings")
def get_settings() -> dict:
    return _settings_response()


@app.put("/api/settings")
def update_settings(request: SettingsUpdate) -> dict:
    settings.update_settings(request.model_dump())
    try:
        manager.update_all_job_commands(default_audio_url(), DEFAULT_VOLUME)
    except CronError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return _settings_response()


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
    from pathlib import Path
    
    json_path = Path(__file__).resolve().parent.parent / "prayer_times.json"
    if not json_path.exists():
        return {"error": "prayer_times.json not found"}
        
    try:
        with open(json_path, "r") as f:
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


@app.get("/audio/{filename}")
def audio(filename: str, request: Request) -> StreamingResponse:
    path = (AUDIO_DIR / filename).resolve()
    if path.parent != AUDIO_DIR or path.suffix.lower() != ".mp3" or not path.exists():
        raise HTTPException(status_code=404, detail="Audio file not found")

    file_size = path.stat().st_size
    range_header = request.headers.get("range")
    media_type = mimetypes.guess_type(path.name)[0] or "audio/mpeg"

    def iter_file(start: int, end: int):
        with open(path, "rb") as f:
            f.seek(start)
            remaining = end - start + 1
            while remaining > 0:
                chunk = f.read(min(64 * 1024, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
                yield chunk

    if range_header:
        try:
            units, byte_range = range_header.split("=", 1)
            if units != "bytes":
                raise ValueError
            start_text, end_text = byte_range.split("-", 1)
            start = int(start_text) if start_text else 0
            end = int(end_text) if end_text else file_size - 1
            if start < 0 or end >= file_size or start > end:
                raise ValueError
        except ValueError as exc:
            raise HTTPException(status_code=416, detail="Invalid range") from exc

        headers = {
            "Accept-Ranges": "bytes",
            "Content-Range": f"bytes {start}-{end}/{file_size}",
            "Content-Length": str(end - start + 1),
        }
        return StreamingResponse(
            iter_file(start, end),
            status_code=206,
            media_type=media_type,
            headers=headers,
        )

    headers = {
        "Accept-Ranges": "bytes",
        "Content-Length": str(file_size),
    }
    return StreamingResponse(
        iter_file(0, file_size - 1),
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
