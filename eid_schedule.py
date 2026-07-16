from __future__ import annotations

from dataclasses import dataclass
from datetime import date, datetime
import json
import os
from pathlib import Path
import threading
from typing import Callable, Mapping

from adhan_config import DATA_DIR, DEFAULT_VOLUME, default_takbeer_url


DEFAULT_START = "07:00"
DEFAULT_END = "09:00"
DEFAULT_INTERVAL = 15
RECOVERY_MINUTES = 2
STATE_FILE = DATA_DIR / "eid_takbeer_state.json"

_state_lock = threading.Lock()


@dataclass(frozen=True)
class EidStatus:
    active: bool
    kind: str
    fitr_configured: bool
    adha_configured: bool


def parse_date(value: str) -> date:
    try:
        parsed = date.fromisoformat(value)
    except (TypeError, ValueError) as exc:
        raise ValueError("Eid dates must use YYYY-MM-DD") from exc
    if parsed.isoformat() != value:
        raise ValueError("Eid dates must use YYYY-MM-DD")
    return parsed


def parse_time(value: str) -> int:
    try:
        hour_text, minute_text = value.split(":", 1)
        if len(hour_text) != 2 or len(minute_text) != 2:
            raise ValueError
        hour = int(hour_text)
        minute = int(minute_text)
    except (AttributeError, TypeError, ValueError) as exc:
        raise ValueError("Takbeer times must use HH:MM") from exc
    if not 0 <= hour <= 23 or not 0 <= minute <= 59:
        raise ValueError("Takbeer times must be valid 24-hour values")
    return hour * 60 + minute


def validated_schedule(settings: Mapping[str, str]) -> tuple[int, int, int]:
    start = parse_time(settings.get("eid_takbeer_start") or DEFAULT_START)
    end = parse_time(settings.get("eid_takbeer_end") or DEFAULT_END)
    try:
        interval = int(settings.get("eid_takbeer_interval") or DEFAULT_INTERVAL)
    except (TypeError, ValueError) as exc:
        raise ValueError("Takbeer interval must be a whole number of minutes") from exc
    if end < start:
        raise ValueError("Takbeer finish time must not be earlier than its start time")
    if not 5 <= interval <= 120:
        raise ValueError("Takbeer interval must be between 5 and 120 minutes")
    return start, end, interval


def validate_eid_settings(settings: Mapping[str, str]) -> None:
    for key in ("eid_fitr_date", "eid_adha_date"):
        value = settings.get(key, "")
        if value:
            parse_date(value)
    validated_schedule(settings)


def status_for_date(settings: Mapping[str, str], current_date: date) -> EidStatus:
    fitr_value = settings.get("eid_fitr_date", "")
    adha_value = settings.get("eid_adha_date", "")
    fitr = parse_date(fitr_value) if fitr_value else None
    adha = parse_date(adha_value) if adha_value else None
    if fitr == current_date:
        return EidStatus(True, "Eid al-Fitr", True, adha is not None)
    if adha == current_date:
        return EidStatus(True, "Eid al-Adha", fitr is not None, True)
    return EidStatus(False, "", fitr is not None, adha is not None)


def latest_due_slot(
    settings: Mapping[str, str],
    now: datetime,
    recovery_minutes: int = RECOVERY_MINUTES,
) -> int | None:
    status = status_for_date(settings, now.date())
    if not status.active or recovery_minutes < 0:
        return None
    start, end, interval = validated_schedule(settings)
    current = now.hour * 60 + now.minute
    if current < start:
        return None
    capped = min(current, end)
    slot = start + ((capped - start) // interval) * interval
    return slot if current - slot <= recovery_minutes else None


def next_slot(settings: Mapping[str, str], now: datetime) -> datetime | None:
    if not status_for_date(settings, now.date()).active:
        return None
    start, end, interval = validated_schedule(settings)
    current = now.hour * 60 + now.minute
    if current < start or (current == start and now.second == 0):
        slot = start
    else:
        elapsed = current - start
        remainder = elapsed % interval
        slot = current if remainder == 0 and now.second == 0 else current + (interval - remainder)
    if slot > end:
        return None
    return now.replace(hour=slot // 60, minute=slot % 60, second=0, microsecond=0)


def _slot_key(now: datetime, slot: int) -> str:
    return f"{now.date().isoformat()}:{slot // 60:02d}:{slot % 60:02d}"


def _read_state(path: Path) -> dict[str, str]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return {}
    return data if isinstance(data, dict) else {}


def _write_state(path: Path, payload: dict[str, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    os.chmod(temporary, 0o600)
    temporary.replace(path)


def _reserve_slot(path: Path, key: str) -> bool:
    with _state_lock:
        state = _read_state(path)
        if state.get("last_slot") == key:
            return False
        _write_state(path, {"last_slot": key, "reserved_at": datetime.now().isoformat()})
        return True


def _release_slot(path: Path, key: str) -> None:
    with _state_lock:
        state = _read_state(path)
        if state.get("last_slot") == key:
            try:
                path.unlink()
            except FileNotFoundError:
                pass


def dispatch_eid_if_due(
    now: datetime | None = None,
    *,
    current_settings: Mapping[str, str] | None = None,
    state_file: Path = STATE_FILE,
    trigger_func: Callable[[str, float], None] | None = None,
) -> bool:
    from adhan_web_ui.settings_manager import SettingsManager
    from adhan_config import SETTINGS_FILE
    from trigger_ha import trigger

    now = now or datetime.now()
    if current_settings is None:
        current_settings = SettingsManager(SETTINGS_FILE).get_settings()
    slot = latest_due_slot(current_settings, now)
    if slot is None:
        return False
    key = _slot_key(now, slot)
    if not _reserve_slot(state_file, key):
        return False
    try:
        (trigger_func or trigger)(default_takbeer_url(), float(DEFAULT_VOLUME))
    except Exception:
        _release_slot(state_file, key)
        raise
    return True


def main() -> int:
    try:
        dispatch_eid_if_due()
    except ValueError as exc:
        print(f"Eid takbeer is not configured: {exc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
