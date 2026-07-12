import tempfile
import textwrap
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from cron_manager import AdhanCronManager, FileCrontabBackend  # noqa: E402


class CronManagerTests(unittest.TestCase):
    def make_manager(self, content: str):
        tempdir = tempfile.TemporaryDirectory()
        path = Path(tempdir.name) / "crontab.txt"
        path.write_text(textwrap.dedent(content).lstrip(), encoding="utf-8")
        manager = AdhanCronManager(FileCrontabBackend(path))
        return manager, path, tempdir

    def test_list_jobs_infers_missing_labels(self):
        manager, path, tempdir = self.make_manager(
            """
            MAILTO=""
            0 5 * * * cd /app && python3 /app/trigger_ha.py http://adhan.local/audio/adhan_final.mp3 1.0 >> /data/adhan.log 2>&1
            30 13 * * * cd /app && python3 /app/trigger_ha.py http://adhan.local/audio/adhan_final.mp3 1.0 >> /data/adhan.log 2>&1
            # [Adhan: Asr]
            # 0 16 * * * cd /app && python3 /app/trigger_ha.py http://adhan.local/audio/adhan_final.mp3 1.0 >> /data/adhan.log 2>&1
            """
        )
        try:
            jobs = manager.list_jobs()
            self.assertEqual([job.label for job in jobs], ["Fajr", "Dhuhr", "Asr"])
            saved = path.read_text(encoding="utf-8")
            self.assertIn("# [Adhan: Fajr]", saved)
            self.assertIn("# [Adhan: Dhuhr]", saved)
            self.assertIn("# [Adhan: Asr]", saved)
        finally:
            tempdir.cleanup()

    def test_list_jobs_accepts_quoted_macos_desktop_script_path(self):
        manager, path, tempdir = self.make_manager(
            """
            # [Adhan: Fajr]
            13 3 * * * cd '/private/var/folders/example/AppTranslocation/Adhancron 2.app/Contents/Resources' && . '/Users/example/Library/Application Support/Adhancron/adhan.env' && /usr/local/bin/python '/private/var/folders/example/AppTranslocation/Adhancron 2.app/Contents/Resources/trigger_ha.py' http://192.168.1.52:8090/audio/adhan_final.mp3 0.8 >> '/Users/example/Library/Application Support/Adhancron/adhan.log' 2>&1
            """
        )
        try:
            jobs = manager.list_jobs()
            self.assertEqual(len(jobs), 1)
            self.assertEqual(jobs[0].label, "Fajr")
            self.assertEqual(
                jobs[0].audio_url,
                "http://192.168.1.52:8090/audio/adhan_final.mp3",
            )
            self.assertEqual(jobs[0].volume, "0.8")
        finally:
            tempdir.cleanup()

    def test_update_jobs_changes_time_and_toggle(self):
        manager, path, tempdir = self.make_manager(
            """
            # [Adhan: Maghrib]
            30 18 * * * cd /app && python3 /app/trigger_ha.py http://adhan.local/audio/adhan_final.mp3 1.0 >> /data/adhan.log 2>&1
            """
        )
        try:
            job = manager.list_jobs()[0]
            manager.update_jobs(
                [
                    {
                        "id": job.job_id,
                        "enabled": False,
                        "time": "18:45",
                    }
                ]
            )
            saved = path.read_text(encoding="utf-8")
            self.assertIn("# [Adhan: Maghrib]", saved)
            self.assertIn("# 45 18 * * * cd /app", saved)
        finally:
            tempdir.cleanup()

    def test_update_all_job_commands_rewrites_audio_url(self):
        manager, path, tempdir = self.make_manager(
            """
            # [Adhan: Dhuhr]
            34 15 * * * cd /app && python3 /app/trigger_ha.py http://adhan-manager.local:8090/audio/adhan_final.mp3 0.8 >> /data/adhan.log 2>&1
            """
        )
        try:
            jobs = manager.update_all_job_commands(
                "http://192.168.1.16:8090/audio/adhan_final.mp3",
                "0.8",
            )
            self.assertEqual(jobs[0].audio_url, "http://192.168.1.16:8090/audio/adhan_final.mp3")
            saved = path.read_text(encoding="utf-8")
            self.assertIn("34 15 * * * cd /app", saved)
            self.assertIn("http://192.168.1.16:8090/audio/adhan_final.mp3 0.8", saved)
            self.assertNotIn("adhan-manager.local", saved)
        finally:
            tempdir.cleanup()

    def test_update_all_job_commands_can_replace_full_command(self):
        manager, path, tempdir = self.make_manager(
            """
            # [Adhan: Dhuhr]
            34 15 * * * cd /old && python3 /app/trigger_ha.py http://adhan-manager.local:8090/audio/adhan_final.mp3 0.8 >> /tmp/old.log 2>&1
            """
        )
        try:
            manager.update_all_job_commands(
                "http://192.168.1.16:8090/audio/adhan_final.mp3",
                "0.8",
                "cd /app && . /data/adhan.env && /usr/local/bin/python /app/trigger_ha.py http://192.168.1.16:8090/audio/adhan_final.mp3 0.8 >> /data/adhan.log 2>&1",
            )
            saved = path.read_text(encoding="utf-8")
            self.assertIn("34 15 * * * cd /app && . /data/adhan.env", saved)
            self.assertNotIn("cd /old", saved)
            self.assertNotIn("/tmp/old.log", saved)
        finally:
            tempdir.cleanup()

    def test_update_jobs_can_refresh_audio_url(self):
        manager, path, tempdir = self.make_manager(
            """
            # [Adhan: Dhuhr]
            34 15 * * * cd /app && python3 /app/trigger_ha.py http://adhan-manager.local:8090/audio/adhan_final.mp3 0.8 >> /data/adhan.log 2>&1
            """
        )
        try:
            job = manager.list_jobs()[0]
            manager.update_jobs(
                [
                    {
                        "id": job.job_id,
                        "enabled": True,
                        "time": "15:35",
                        "audio_url": "http://192.168.1.16:8090/audio/adhan_final.mp3",
                        "volume": "0.8",
                    }
                ]
            )
            saved = path.read_text(encoding="utf-8")
            self.assertIn("35 15 * * * cd /app", saved)
            self.assertIn("http://192.168.1.16:8090/audio/adhan_final.mp3 0.8", saved)
            self.assertNotIn("adhan-manager.local", saved)
        finally:
            tempdir.cleanup()

    def test_update_jobs_can_replace_full_command(self):
        manager, path, tempdir = self.make_manager(
            """
            # [Adhan: Fajr]
            13 3 * * * cd /old && python3 /app/trigger_ha.py http://adhan-manager.local:8090/audio/adhan_final.mp3 0.8 >> /tmp/old.log 2>&1
            """
        )
        try:
            job = manager.list_jobs()[0]
            manager.update_jobs(
                [
                    {
                        "id": job.job_id,
                        "enabled": True,
                        "time": "03:14",
                        "command": "cd /app && . /data/adhan.env && /usr/local/bin/python /app/trigger_ha.py http://192.168.1.16:8090/audio/adhan_final.mp3 0.8 >> /data/adhan.log 2>&1",
                    }
                ]
            )
            saved = path.read_text(encoding="utf-8")
            self.assertIn("14 3 * * * cd /app && . /data/adhan.env", saved)
            self.assertIn("/usr/local/bin/python /app/trigger_ha.py", saved)
            self.assertIn(">> /data/adhan.log 2>&1", saved)
            self.assertNotIn("/tmp/old.log", saved)
            self.assertNotIn("cd /old", saved)
        finally:
            tempdir.cleanup()


if __name__ == "__main__":
    unittest.main()
