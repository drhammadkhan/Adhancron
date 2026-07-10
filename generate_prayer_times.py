from __future__ import annotations

import json
import os
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

import requests

from adhan_config import PRAYER_TIMES_FILE, SETTINGS_FILE

ALISLAM_MONTHLY_TIMINGS_URL = "https://www.alislam.org/adhan/api/timings/month"
REQUEST_TIMEOUT = float(os.getenv("ADHAN_TIMINGS_REQUEST_TIMEOUT", "20"))
PRAYER_NAME_MAP = {
    "Fajr": "Fajr",
    "Zuhr": "Dhuhr",
    "Asr": "Asr",
    "Maghrib": "Maghrib",
    "Isha": "Isha",
}
PRAYER_NAMES = set(PRAYER_NAME_MAP.values())


@dataclass(frozen=True)
class Location:
    latitude: float
    longitude: float


@dataclass(frozen=True)
class GenerationResult:
    years: tuple[int, ...]
    output_file: Path
    timezone: str
    summary: str


def _load_saved_settings() -> dict[str, str]:
    try:
        with open(SETTINGS_FILE, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}
    return {key: value for key, value in data.items() if isinstance(value, str)}


def location_from_values(latitude: str, longitude: str) -> Location:
    try:
        parsed_latitude = float(latitude)
        parsed_longitude = float(longitude)
    except ValueError as exc:
        raise ValueError("Latitude and longitude must be numbers") from exc
    if not -90 <= parsed_latitude <= 90 or not -180 <= parsed_longitude <= 180:
        raise ValueError("Latitude must be between -90 and 90 and longitude between -180 and 180")
    return Location(parsed_latitude, parsed_longitude)


def configured_location() -> Location:
    """Return the persisted home location, preferring web UI settings over env."""
    settings = _load_saved_settings()
    latitude = settings.get("latitude", "").strip() or os.getenv("ADHAN_LATITUDE", "").strip()
    longitude = settings.get("longitude", "").strip() or os.getenv("ADHAN_LONGITUDE", "").strip()
    if not latitude or not longitude:
        raise ValueError("Set latitude and longitude in the web UI before generating prayer times")
    return location_from_values(latitude, longitude)


def _format_month(payload: dict) -> tuple[dict[str, dict[str, dict[str, str]]], str]:
    location_info = payload.get("locationInfo", {})
    timezone_name = location_info.get("timezone")
    if not isinstance(timezone_name, str):
        raise ValueError("Alislam did not return a timezone for this location")
    try:
        timezone = ZoneInfo(timezone_name)
    except ZoneInfoNotFoundError as exc:
        raise ValueError(f"Alislam returned an invalid timezone: {timezone_name}") from exc

    result: dict[str, dict[str, dict[str, str]]] = {}
    for day in payload.get("multiDayTimings", []):
        prayers = day.get("prayers", [])
        prayer_values = {
            PRAYER_NAME_MAP[prayer.get("name")]: prayer.get("time")
            for prayer in prayers
            if prayer.get("name") in PRAYER_NAME_MAP
        }
        if set(prayer_values) != PRAYER_NAMES:
            raise ValueError("Alislam returned incomplete prayer times")
        times = {}
        for name, timestamp in prayer_values.items():
            if not isinstance(timestamp, (int, float)):
                raise ValueError(f"Alislam returned an invalid {name} timestamp")
            times[name] = datetime.fromtimestamp(timestamp / 1000, tz=timezone).strftime("%H:%M")
        fajr_timestamp = prayer_values["Fajr"]
        local_day = datetime.fromtimestamp(fajr_timestamp / 1000, tz=timezone)
        result.setdefault(str(local_day.year), {}).setdefault(local_day.strftime("%B"), {})[
            str(local_day.day)
        ] = times
    return result, timezone_name


def _fetch_month(year: int, month: int, location: Location) -> tuple[dict, str]:
    try:
        response = requests.get(
            ALISLAM_MONTHLY_TIMINGS_URL,
            params={
                "year": year,
                "month": month,
                "lat": format(location.latitude, ".8f"),
                "lng": format(location.longitude, ".8f"),
            },
            timeout=REQUEST_TIMEOUT,
        )
        response.raise_for_status()
        return _format_month(response.json())
    except requests.RequestException as exc:
        raise ValueError(f"Unable to fetch prayer times from Alislam: {exc}") from exc


def generate_configured_times(now: datetime | None = None) -> GenerationResult:
    """Fetch the official Alislam timetable and save it in Adhancron's JSON format."""
    now = now or datetime.now()
    location = configured_location()
    years = (now.year, now.year + 1)
    timetable: dict[str, dict] = {}
    returned_timezone = ""

    for year in years:
        timetable[str(year)] = {}
        for month in range(1, 13):
            month_data, timezone_name = _fetch_month(year, month, location)
            returned_timezone = returned_timezone or timezone_name
            if timezone_name != returned_timezone:
                raise ValueError("Alislam returned inconsistent timezones for this location")
            timetable[str(year)].update(month_data.get(str(year), {}))

    PRAYER_TIMES_FILE.parent.mkdir(parents=True, exist_ok=True)
    temp_file = PRAYER_TIMES_FILE.with_suffix(".tmp")
    with open(temp_file, "w", encoding="utf-8") as f:
        json.dump(timetable, f, indent=2)
        f.write("\n")
    temp_file.replace(PRAYER_TIMES_FILE)
    return GenerationResult(
        years=years,
        output_file=PRAYER_TIMES_FILE,
        timezone=returned_timezone,
        summary=(
            f"Generated official Alislam times for {years[0]} and {years[1]} at "
            f"{location.latitude:.5f}, {location.longitude:.5f} ({returned_timezone})"
        ),
    )


def ensure_configured_times(now: datetime | None = None) -> GenerationResult | None:
    """Generate only when the persistent timetable does not cover the next year."""
    now = now or datetime.now()
    required_years = {str(now.year), str(now.year + 1)}
    try:
        with open(PRAYER_TIMES_FILE, "r", encoding="utf-8") as f:
            existing = json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        existing = {}
    if required_years.issubset(existing):
        return None
    return generate_configured_times(now)


if __name__ == "__main__":
    print(generate_configured_times().summary)
