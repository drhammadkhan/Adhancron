from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
LITE = ROOT / "raspberry-pi" / "lite"


class PiLiteAssetsTests(unittest.TestCase):
    def test_installer_is_native_and_arm64(self):
        installer = (LITE / "install.sh").read_text(encoding="utf-8")
        self.assertIn("aarch64", installer)
        self.assertIn("python3 -m venv", installer)
        self.assertIn("cage cog", installer)
        self.assertNotIn("docker.io", installer)
        self.assertNotIn("chromium", installer.lower())

    def test_display_prefers_direct_drm_with_wayland_fallback(self):
        kiosk = (LITE / "kiosk.sh").read_text(encoding="utf-8")
        self.assertIn("cog --platform=drm", kiosk)
        self.assertIn("cage -s -- cog --platform=wl", kiosk)
        self.assertNotIn("--fullscreen", kiosk)

    def test_native_updater_has_health_check_and_rollback(self):
        updater = (LITE / "update-native.sh").read_text(encoding="utf-8")
        self.assertIn("/api/jobs", updater)
        self.assertIn("CURRENT_RELEASE", updater)
        self.assertIn("restoring the previous release", updater)

    def test_native_runtime_uses_release_python(self):
        service = (LITE / "adhancron.service").read_text(encoding="utf-8")
        startup = (LITE / "native-start.sh").read_text(encoding="utf-8")
        self.assertIn("ADHAN_PYTHON=/opt/adhancron/venv/bin/python", service)
        self.assertIn("export ADHAN_PYTHON", startup)
        self.assertIn("$PYTHON_EXECUTABLE $APP_DIR/update_adhan.py", startup)

    def test_lite_profile_has_required_units_and_docs(self):
        expected = {
            "README.md",
            "install.sh",
            "native-start.sh",
            "kiosk.sh",
            "update-native.sh",
            "adhancron.service",
            "adhanclock-display.service",
            "adhancron-native-update.service",
            "adhancron-native-update.timer",
            "adhancron.env.example",
        }
        self.assertTrue(expected.issubset({path.name for path in LITE.iterdir()}))


if __name__ == "__main__":
    unittest.main()
