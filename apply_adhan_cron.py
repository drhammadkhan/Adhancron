from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path
import sys

from adhan_config import (
    DEFAULT_VOLUME,
    OVERRIDE_FILE,
    PRAYER_ORDER,
    PRAYER_TIMES_FILE,
    STATUS_FILE,
    default_audio_url,
    trigger_command,
)

sys.path.insert(0, str(Path(__file__).resolve().parent / "adhan_web_ui"))
from cron_manager import AdhanCronManager, CronError  # noqa: E402


def _load_json(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _today_times(now: datetime) -> dict[str, str]:
    data = _load_json(PRAYER_TIMES_FILE)
    year_data = data.get(str(now.year), {})
    month_data = year_data.get(now.strftime("%B"), {})
    return month_data.get(str(now.day), {})


def _load_overrides() -> dict[str, bool]:
    if not OVERRIDE_FILE.exists():
        return {}
    try:
        return _load_json(OVERRIDE_FILE)
    except Exception:
        return {}


def _write_status(ok: bool, result: str, summary: str) -> None:
    STATUS_FILE.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "ok": ok,
        "result": result,
        "lastRun": datetime.now().isoformat(),
        "summary": summary,
    }
    with open(STATUS_FILE, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)


def _seed_jobs(manager: AdhanCronManager, day_times: dict[str, str]) -> None:
    existing = manager.list_jobs()
    existing_labels = {job.label for job in existing if job.label}
    missing_labels = [label for label in PRAYER_ORDER if label not in existing_labels]
    if not missing_labels:
        return

    crontab = manager.backend.read()
    additions: list[str] = []
    for label in missing_labels:
        time_value = day_times.get(label, "00:00")
        hour, minute = time_value.split(":")
        additions.extend(
            [
                f"# [Adhan: {label}]",
                f"{int(minute)} {int(hour)} * * * {trigger_command(volume=DEFAULT_VOLUME)}",
            ]
        )

    next_crontab = crontab.rstrip()
    if next_crontab:
        next_crontab += "\n\n"
    next_crontab += "\n".join(additions) + "\n"
    manager.backend.write(next_crontab)


def apply_updates(now: datetime | None = None) -> int:
    now = now or datetime.now()
    if not PRAYER_TIMES_FILE.exists():
        message = f"Prayer times file not found: {PRAYER_TIMES_FILE}"
        print(message)
        _write_status(False, "missing_prayer_times", message)
        return 1

    day_times = _today_times(now)
    if not day_times:
        message = f"No prayer times found for {now.strftime('%-d %B %Y')} in {PRAYER_TIMES_FILE}"
        print(message)
        _write_status(False, "missing_today", message)
        return 0

    manager = AdhanCronManager()
    _seed_jobs(manager, day_times)
    current_audio_url = default_audio_url()
    overrides = _load_overrides()
    updates = []
    messages = []

    for job in manager.list_jobs():
        prayer_name = job.label
        if not prayer_name:
            continue

        if overrides.get(prayer_name):
            messages.append(f"Skipping {prayer_name}: manual override is active")
            continue

        new_time = day_times.get(prayer_name)
        if not new_time:
            messages.append(f"No {prayer_name} time in today's prayer data")
            continue

        command_changed = job.audio_url != current_audio_url or job.volume != DEFAULT_VOLUME
        if job.time != new_time or command_changed:
            changes = []
            if job.time != new_time:
                changes.append(f"time {job.time} -> {new_time}")
            if command_changed:
                changes.append("audio command")
            messages.append(f"Updating {prayer_name}: {', '.join(changes)}")
            updates.append(
                {
                    "id": job.job_id,
                    "time": new_time,
                    "enabled": job.enabled,
                    "audio_url": current_audio_url,
                    "volume": DEFAULT_VOLUME,
                }
            )
        else:
            messages.append(f"{prayer_name} already up to date at {new_time}")

    if updates:
        manager.update_jobs(updates)
        messages.append("Crontab updated successfully")
    else:
        messages.append("No crontab changes needed")

    summary = "\n".join(messages)
    print(summary)
    _write_status(True, "updated", summary)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(apply_updates())
    except CronError as exc:
        _write_status(False, "cron_error", str(exc))
        raise
