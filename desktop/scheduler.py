from __future__ import annotations

from datetime import datetime
import logging
import threading


class DesktopScheduler:
    """Dispatch the persisted prayer schedule without relying on an OS crontab."""

    def __init__(self, interval_seconds: int = 15) -> None:
        self.interval_seconds = interval_seconds
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._fired: set[str] = set()
        self._last_refresh_date: str | None = None

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._thread = threading.Thread(target=self._run, name="adhancron-scheduler", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                self._tick()
            except Exception:
                logging.exception("Desktop schedule check failed")
            self._stop.wait(self.interval_seconds)

    def _tick(self) -> None:
        from adhan_config import DEFAULT_VOLUME, default_audio_url
        from adhan_web_ui.app import manager
        from apply_adhan_cron import apply_updates
        from generate_prayer_times import generate_configured_times
        from trigger_ha import trigger

        now = datetime.now()
        date_key = now.date().isoformat()
        if self._last_refresh_date != date_key:
            try:
                generate_configured_times(now)
            except ValueError:
                # First-run setup has not supplied a location yet.
                pass
            else:
                apply_updates(now)
            self._last_refresh_date = date_key
            self._fired.clear()

        minute_key = now.strftime("%H:%M")
        for job in manager.list_jobs():
            key = f"{date_key}:{job.label}:{job.time}"
            if not job.enabled or job.time != minute_key or key in self._fired:
                continue
            self._fired.add(key)
            threading.Thread(
                target=trigger,
                args=(default_audio_url(), float(DEFAULT_VOLUME)),
                name=f"adhan-{job.label}",
                daemon=True,
            ).start()
