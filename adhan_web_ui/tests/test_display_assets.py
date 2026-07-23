from pathlib import Path
import unittest


STATIC = Path(__file__).resolve().parents[1] / "static"


class DisplayAssetsTests(unittest.TestCase):
    def test_display_is_fully_local(self):
        html = (STATIC / "display.html").read_text(encoding="utf-8")
        self.assertIn('/static/display.css', html)
        self.assertIn('/static/display.js', html)
        self.assertNotIn('https://', html)
        self.assertNotIn('http://', html)

    def test_clock_digits_do_not_shift(self):
        css = (STATIC / "display.css").read_text(encoding="utf-8")
        self.assertIn("font-variant-numeric: tabular-nums", css)

    def test_display_has_setup_state_and_both_faces(self):
        html = (STATIC / "display.html").read_text(encoding="utf-8")
        css = (STATIC / "display.css").read_text(encoding="utf-8")
        self.assertIn('id="setup-state"', html)
        self.assertIn('[data-face="focus"]', css)


if __name__ == "__main__":
    unittest.main()
