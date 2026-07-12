from __future__ import annotations

import json
import math
import os
from dataclasses import dataclass
from datetime import date, datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from astral import Observer, SunDirection
from astral.sun import (
    julianday,
    julianday_to_juliancentury,
    noon,
    sun_declination,
    sunrise,
    sunset,
    time_at_elevation,
)

from adhan_config import PRAYER_TIMES_FILE, SETTINGS_FILE

FAJR_OFFSET = timedelta(minutes=90)
DHUHR_OFFSET = timedelta(minutes=5)
MAGHRIB_OFFSET = timedelta(minutes=1)


@dataclass(frozen=True)
class Location:
    latitude: float
    longitude: float
    timezone: ZoneInfo


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


def _configured_timezone() -> ZoneInfo:
    timezone_name = os.getenv("TZ", "UTC")
    try:
        return ZoneInfo(timezone_name)
    except ZoneInfoNotFoundError as exc:
        raise ValueError(f"Invalid timezone: {timezone_name}") from exc


def _timezone_for_coordinates(latitude: float, longitude: float) -> ZoneInfo:
    """Resolve the prayer location's IANA timezone, independent of the host."""
    try:
        from timezonefinder import timezone_at
    except ImportError as exc:
        raise ValueError("Timezone lookup support is not installed") from exc

    timezone_name = timezone_at(lng=longitude, lat=latitude)
    if not timezone_name:
        raise ValueError("Unable to determine a timezone for this location")
    try:
        return ZoneInfo(timezone_name)
    except ZoneInfoNotFoundError as exc:
        raise ValueError(f"Invalid timezone for this location: {timezone_name}") from exc


def location_from_values(latitude: str, longitude: str) -> Location:
    try:
        parsed_latitude = float(latitude)
        parsed_longitude = float(longitude)
    except ValueError as exc:
        raise ValueError("Latitude and longitude must be numbers") from exc
    if not -90 <= parsed_latitude <= 90 or not -180 <= parsed_longitude <= 180:
        raise ValueError("Latitude must be between -90 and 90 and longitude between -180 and 180")
    return Location(
        parsed_latitude,
        parsed_longitude,
        _timezone_for_coordinates(parsed_latitude, parsed_longitude),
    )


def configured_location() -> Location:
    """Return the persisted home location, preferring web UI settings over env."""
    settings = _load_saved_settings()
    latitude = settings.get("latitude", "").strip() or os.getenv("ADHAN_LATITUDE", "").strip()
    longitude = settings.get("longitude", "").strip() or os.getenv("ADHAN_LONGITUDE", "").strip()
    if not latitude or not longitude:
        raise ValueError("Set latitude and longitude in the web UI before generating prayer times")
    return location_from_values(latitude, longitude)


def _round_to_minute(value: datetime) -> datetime:
    return (value + timedelta(seconds=30)).replace(second=0, microsecond=0)


def _days_since_solstice(day: date, latitude: float) -> int:
    day_of_year = day.timetuple().tm_yday
    days_in_year = 366 if day.year % 4 == 0 and (day.year % 100 != 0 or day.year % 400 == 0) else 365
    if latitude >= 0:
        return (day_of_year + 10) % days_in_year
    southern_offset = 173 if days_in_year == 366 else 172
    return (day_of_year - southern_offset) % days_in_year


def _linear_seasonal_value(days: int, a: float, b: float, c: float, d: float) -> float:
    if days < 91:
        return a + ((b - a) / 91.0) * days
    if days < 137:
        return b + ((c - b) / 46.0) * (days - 91)
    if days < 183:
        return c + ((d - c) / 46.0) * (days - 137)
    if days < 229:
        return d + ((c - d) / 46.0) * (days - 183)
    if days < 275:
        return c + ((b - c) / 46.0) * (days - 229)
    return b + ((a - b) / 91.0) * (days - 275)


def _isha_offset_seconds(day: date, latitude: float) -> int:
    """Moonsighting Committee's seasonal evening-twilight adjustment."""
    absolute_latitude = abs(latitude)
    a = 75 + (25.6 / 55.0) * absolute_latitude
    b = 75 + (2.05 / 55.0) * absolute_latitude
    c = 75 - (9.21 / 55.0) * absolute_latitude
    d = 75 + (6.14 / 55.0) * absolute_latitude
    return round(_linear_seasonal_value(_days_since_solstice(day, latitude), a, b, c, d) * 60)


def _asr_elevation(day: date, latitude: float) -> float:
    """Solar elevation for standard Asr (shadow factor 1)."""
    century = julianday_to_juliancentury(julianday(day))
    declination = sun_declination(century)
    noon_shadow = math.tan(math.radians(abs(latitude - declination)))
    return math.degrees(math.atan(1 / (1 + noon_shadow)))


def calculate_day(day: date, location: Location) -> dict[str, str]:
    """Calculate local Alislam-compatible prayer times for one Gregorian day."""
    observer = Observer(latitude=location.latitude, longitude=location.longitude)
    try:
        local_sunrise = sunrise(observer, date=day, tzinfo=location.timezone)
        local_sunset = sunset(observer, date=day, tzinfo=location.timezone)
    except ValueError as exc:
        raise ValueError("Sunrise and sunset are unavailable for this date and location") from exc

    asr = time_at_elevation(
        observer,
        elevation=_asr_elevation(day, location.latitude),
        date=day,
        direction=SunDirection.SETTING,
        tzinfo=location.timezone,
    )
    values = {
        "Fajr": local_sunrise - FAJR_OFFSET,
        "Dhuhr": noon(observer, date=day, tzinfo=location.timezone) + DHUHR_OFFSET,
        "Asr": asr,
        "Maghrib": local_sunset + MAGHRIB_OFFSET,
        "Isha": local_sunset + timedelta(seconds=_isha_offset_seconds(day, location.latitude)),
    }
    return {name: _round_to_minute(value).strftime("%H:%M") for name, value in values.items()}


def calculate_year(year: int, location: Location) -> dict[str, dict[str, dict[str, str]]]:
    result: dict[str, dict[str, dict[str, str]]] = {str(year): {}}
    current = date(year, 1, 1)
    while current.year == year:
        month = current.strftime("%B")
        result[str(year)].setdefault(month, {})[str(current.day)] = calculate_day(current, location)
        current += timedelta(days=1)
    return result


def generate_configured_times(now: datetime | None = None) -> GenerationResult:
    """Generate the current and following year's timetable without a network request."""
    now = now or datetime.now()
    location = configured_location()
    years = (now.year, now.year + 1)
    timetable: dict[str, dict] = {}
    for year in years:
        timetable.update(calculate_year(year, location))

    PRAYER_TIMES_FILE.parent.mkdir(parents=True, exist_ok=True)
    temp_file = PRAYER_TIMES_FILE.with_suffix(".tmp")
    with open(temp_file, "w", encoding="utf-8") as f:
        json.dump(timetable, f, indent=2)
        f.write("\n")
    temp_file.replace(PRAYER_TIMES_FILE)
    return GenerationResult(
        years=years,
        output_file=PRAYER_TIMES_FILE,
        timezone=location.timezone.key,
        summary=(
            f"Generated local prayer times for {years[0]} and {years[1]} at "
            f"{location.latitude:.5f}, {location.longitude:.5f} ({location.timezone.key})"
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
