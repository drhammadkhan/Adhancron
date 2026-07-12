from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
import re
import subprocess
import threading
from pathlib import Path
from typing import Iterable

LABEL_PATTERN = re.compile(r"^\s*#\s*\[Adhan:\s*(?P<label>[^\]]+)\]\s*$")
CRON_PATTERN = re.compile(
    r"^\s*(?P<minute>\d{1,2})\s+(?P<hour>\d{1,2})\s+"
    r"(?P<dom>\S+)\s+(?P<month>\S+)\s+(?P<dow>\S+)\s+(?P<command>.+?)\s*$"
)
DISABLED_CRON_PATTERN = re.compile(
    r"^\s*#\s*(?P<minute>\d{1,2})\s+(?P<hour>\d{1,2})\s+"
    r"(?P<dom>\S+)\s+(?P<month>\S+)\s+(?P<dow>\S+)\s+(?P<command>.+?)\s*$"
)
TRIGGER_PATTERN = re.compile(
    r"trigger_ha\.py\s+(?P<audio_url>\S+)(?:\s+(?P<volume>\S+))?"
)

PRAYER_ORDER = ["Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"]
CANONICAL_TIMES = {
    "Fajr": (5, 12),
    "Dhuhr": (13, 16),
    "Asr": (16, 46),
    "Maghrib": (19, 44),
    "Isha": (21, 21),
}


class CronError(RuntimeError):
    pass


@dataclass
class RawLine:
    text: str


@dataclass
class AdhanJob:
    label: str | None
    minute: int
    hour: int
    dom: str
    month: str
    dow: str
    command: str
    enabled: bool
    audio_url: str
    volume: str

    @property
    def time(self) -> str:
        return f"{self.hour:02d}:{self.minute:02d}"

    @property
    def job_id(self) -> str:
        digest = hashlib.sha1(
            (
                f"{self.label}|{self.hour}|{self.minute}|{self.dom}|{self.month}|"
                f"{self.dow}|{self.audio_url}|{self.volume}|{self.command}"
            ).encode("utf-8")
        ).hexdigest()
        return digest[:12]


class CrontabBackend:
    def read(self) -> str:
        raise NotImplementedError

    def write(self, content: str) -> None:
        raise NotImplementedError


class CliCrontabBackend(CrontabBackend):
    def read(self) -> str:
        result = subprocess.run(
            ["crontab", "-l"],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode == 0:
            return result.stdout
        stderr = (result.stderr or "").strip().lower()
        if "no crontab for" in stderr:
            return ""
        raise CronError(result.stderr.strip() or "Failed to read crontab")

    def write(self, content: str) -> None:
        result = subprocess.run(
            ["crontab", "-"],
            input=content,
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            raise CronError(result.stderr.strip() or "Failed to write crontab")


class FileCrontabBackend(CrontabBackend):
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)

    def read(self) -> str:
        if not self.path.exists():
            return ""
        return self.path.read_text(encoding="utf-8")

    def write(self, content: str) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(content, encoding="utf-8")


def create_runtime_manager(data_dir: str | Path) -> "AdhanCronManager":
    """Use a local schedule file for desktop builds and crontab everywhere else."""
    if os.getenv("ADHAN_RUNTIME") == "desktop":
        return AdhanCronManager(FileCrontabBackend(Path(data_dir) / "desktop_schedule.crontab"))
    return AdhanCronManager()


class AdhanCronManager:
    def __init__(self, backend: CrontabBackend | None = None) -> None:
        self.backend = backend or CliCrontabBackend()
        self._lock = threading.Lock()

    def list_jobs(self) -> list[AdhanJob]:
        with self._lock:
            lines = self._parse_lines(self.backend.read())
            changed = self._ensure_labels(lines)
            if changed:
                self.backend.write(self._serialize(lines))
            return [line for line in lines if isinstance(line, AdhanJob)]

    def update_jobs(self, updates: Iterable[dict]) -> list[AdhanJob]:
        update_map = {item["id"]: item for item in updates}
        with self._lock:
            lines = self._parse_lines(self.backend.read())
            self._ensure_labels(lines)
            jobs = [line for line in lines if isinstance(line, AdhanJob)]
            for job in jobs:
                payload = update_map.get(job.job_id)
                if not payload:
                    continue
                hour, minute = self._parse_time(payload["time"])
                job.hour = hour
                job.minute = minute
                job.enabled = bool(payload["enabled"])
                if payload.get("command"):
                    job.command = payload["command"]
                    trigger_match = TRIGGER_PATTERN.search(job.command)
                    if trigger_match:
                        job.audio_url = trigger_match.group("audio_url")
                        job.volume = trigger_match.group("volume") or "0.8"
                elif payload.get("audio_url") or payload.get("volume"):
                    self._update_trigger_args(
                        job,
                        payload.get("audio_url", job.audio_url),
                        payload.get("volume", job.volume),
                    )
            self.backend.write(self._serialize(lines))
            return [line for line in lines if isinstance(line, AdhanJob)]

    def update_all_job_commands(
        self,
        audio_url: str,
        volume: str,
        command: str | None = None,
    ) -> list[AdhanJob]:
        with self._lock:
            lines = self._parse_lines(self.backend.read())
            self._ensure_labels(lines)
            jobs = [line for line in lines if isinstance(line, AdhanJob)]
            changed = False
            for job in jobs:
                if command and job.command != command:
                    job.command = command
                    job.audio_url = audio_url
                    job.volume = volume
                    changed = True
                    continue
                if job.audio_url == audio_url and job.volume == volume:
                    continue
                self._update_trigger_args(job, audio_url, volume)
                changed = True
            if changed:
                self.backend.write(self._serialize(lines))
            return [line for line in lines if isinstance(line, AdhanJob)]

    def _parse_lines(self, crontab_text: str) -> list[RawLine | AdhanJob]:
        parsed: list[RawLine | AdhanJob] = []
        lines = crontab_text.splitlines()
        idx = 0
        while idx < len(lines):
            line = lines[idx]
            label_match = LABEL_PATTERN.match(line)
            if label_match and idx + 1 < len(lines):
                maybe_job = self._parse_job(lines[idx + 1])
                if maybe_job:
                    maybe_job.label = label_match.group("label").strip()
                    parsed.append(maybe_job)
                    idx += 2
                    continue
            job = self._parse_job(line)
            if job:
                parsed.append(job)
            else:
                parsed.append(RawLine(text=line))
            idx += 1
        return parsed

    def _parse_job(self, line: str) -> AdhanJob | None:
        enabled = True
        match = CRON_PATTERN.match(line)
        if not match:
            match = DISABLED_CRON_PATTERN.match(line)
            enabled = False
        if not match:
            return None

        command = match.group("command").strip()
        if "trigger_ha.py" not in command:
            return None

        trigger_match = TRIGGER_PATTERN.search(command)
        if not trigger_match:
            raise CronError(f"Unable to parse adhan command: {command}")

        return AdhanJob(
            label=None,
            minute=int(match.group("minute")),
            hour=int(match.group("hour")),
            dom=match.group("dom"),
            month=match.group("month"),
            dow=match.group("dow"),
            command=command,
            enabled=enabled,
            audio_url=trigger_match.group("audio_url"),
            volume=trigger_match.group("volume") or "0.8",
        )

    def _ensure_labels(self, lines: list[RawLine | AdhanJob]) -> bool:
        jobs = [line for line in lines if isinstance(line, AdhanJob)]
        unlabeled = [job for job in jobs if not job.label]
        if not unlabeled:
            return False

        assignments = self._infer_labels(jobs)
        changed = False
        for job in jobs:
            if job.label:
                continue
            inferred = assignments[job.job_id]
            job.label = inferred
            changed = True
        return changed

    def _infer_labels(self, jobs: list[AdhanJob]) -> dict[str, str]:
        exact_used: set[str] = set()
        assignments: dict[str, str] = {}
        unlabeled: list[AdhanJob] = []

        for job in jobs:
            if job.label:
                exact_used.add(job.label)
                assignments[job.job_id] = job.label
                continue

            exact_match = self._match_exact(job)
            if exact_match and exact_match not in exact_used:
                assignments[job.job_id] = exact_match
                exact_used.add(exact_match)
            else:
                unlabeled.append(job)

        remaining_labels = [label for label in PRAYER_ORDER if label not in exact_used]
        ordered_jobs = sorted(unlabeled, key=lambda job: (job.hour * 60 + job.minute, job.command))

        if len(ordered_jobs) <= len(remaining_labels):
            for job, label in zip(ordered_jobs, remaining_labels):
                assignments[job.job_id] = label
            return assignments

        available = [label for label in PRAYER_ORDER if label not in exact_used]
        for job in ordered_jobs:
            label = self._nearest_label(job, available)
            assignments[job.job_id] = label
            if label in available:
                available.remove(label)
        return assignments

    def _match_exact(self, job: AdhanJob) -> str | None:
        for label, (hour, minute) in CANONICAL_TIMES.items():
            if job.hour == hour and job.minute == minute:
                return label
        return None

    def _nearest_label(self, job: AdhanJob, available: list[str]) -> str:
        candidates = available or PRAYER_ORDER
        target = job.hour * 60 + job.minute
        return min(
            candidates,
            key=lambda label: abs(target - (CANONICAL_TIMES[label][0] * 60 + CANONICAL_TIMES[label][1])),
        )

    def _update_trigger_args(self, job: AdhanJob, audio_url: str, volume: str) -> None:
        def repl(match: re.Match) -> str:
            return f"trigger_ha.py {audio_url} {volume}"

        job.command = TRIGGER_PATTERN.sub(repl, job.command, count=1)
        job.audio_url = audio_url
        job.volume = volume

    def _serialize(self, lines: list[RawLine | AdhanJob]) -> str:
        output: list[str] = []
        for line in lines:
            if isinstance(line, RawLine):
                output.append(line.text)
                continue
            label = line.label or "Adhan"
            output.append(f"# [Adhan: {label}]")
            cron_line = (
                f"{line.minute} {line.hour} {line.dom} {line.month} {line.dow} {line.command}"
            )
            if line.enabled:
                output.append(cron_line)
            else:
                output.append(f"# {cron_line}")
        return "\n".join(output).rstrip() + "\n"

    def _parse_time(self, value: str) -> tuple[int, int]:
        match = re.fullmatch(r"(?P<hour>\d{2}):(?P<minute>\d{2})", value)
        if not match:
            raise CronError("Time must be in HH:MM format")
        hour = int(match.group("hour"))
        minute = int(match.group("minute"))
        if hour > 23 or minute > 59:
            raise CronError("Time must be a valid 24-hour value")
        return hour, minute
