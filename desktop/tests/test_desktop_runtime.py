from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from adhan_web_ui.cron_manager import CliCrontabBackend, FileCrontabBackend, create_runtime_manager
from desktop import launcher
from desktop.scheduler import DesktopScheduler


class DesktopRuntimeTests(unittest.TestCase):
    def test_macos_bundle_uses_the_resources_directory(self):
        with mock.patch.object(launcher.sys, "frozen", True, create=True), mock.patch.object(
            launcher.sys, "platform", "darwin"
        ), mock.patch.object(launcher.sys, "executable", "/Applications/Adhancron.app/Contents/MacOS/Adhancron"):
            self.assertEqual(
                launcher._bundle_dir(),
                Path("/Applications/Adhancron.app/Contents/Resources"),
            )

    def test_macos_localtime_path_provides_an_iana_timezone(self):
        with mock.patch("desktop.launcher.os.path.realpath", return_value="/var/db/timezone/zoneinfo/Europe/London"):
            self.assertEqual(launcher._system_timezone(), "Europe/London")

    def test_default_runtime_keeps_using_system_crontab(self):
        previous = os.environ.pop("ADHAN_RUNTIME", None)
        try:
            manager = create_runtime_manager(Path("/tmp"))
            self.assertIsInstance(manager.backend, CliCrontabBackend)
        finally:
            if previous is not None:
                os.environ["ADHAN_RUNTIME"] = previous

    def test_desktop_runtime_uses_a_schedule_file(self):
        previous = os.environ.get("ADHAN_RUNTIME")
        os.environ["ADHAN_RUNTIME"] = "desktop"
        try:
            with tempfile.TemporaryDirectory() as tempdir:
                manager = create_runtime_manager(Path(tempdir))
                self.assertIsInstance(manager.backend, FileCrontabBackend)
                manager.backend.write("# desktop schedule\n")
                self.assertEqual(manager.backend.read(), "# desktop schedule\n")
                self.assertTrue((Path(tempdir) / "desktop_schedule.crontab").exists())
        finally:
            if previous is None:
                os.environ.pop("ADHAN_RUNTIME", None)
            else:
                os.environ["ADHAN_RUNTIME"] = previous

    def test_desktop_runtime_uses_the_operating_system_timezone(self):
        keys = ("ADHAN_APP_DIR", "ADHAN_DATA_DIR", "ADHAN_RUNTIME", "PUBLIC_BASE_URL", "TZ")
        previous = {key: os.environ.get(key) for key in keys}
        try:
            for key in keys:
                os.environ.pop(key, None)
            with mock.patch.object(launcher, "_system_timezone", return_value="Europe/London"), mock.patch.object(
                launcher, "_data_dir", return_value=Path(tempfile.gettempdir()) / "Adhancron-timezone-test"
            ), mock.patch.object(launcher.time, "tzset", create=True) as tzset:
                launcher._configure_environment(8090)
            self.assertEqual(os.environ["TZ"], "Europe/London")
            tzset.assert_called_once()
        finally:
            for key, value in previous.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value

    def test_scheduler_checks_immediately_when_started(self):
        scheduler = DesktopScheduler(interval_seconds=3600)
        calls = []

        def tick():
            calls.append(True)
            scheduler._stop.set()

        scheduler._tick = tick  # type: ignore[method-assign]
        scheduler._run()
        self.assertEqual(calls, [True])


if __name__ == "__main__":
    unittest.main()
