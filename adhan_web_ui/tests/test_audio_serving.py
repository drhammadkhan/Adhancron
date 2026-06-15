import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from adhan_web_ui.app import _parse_range_header  # noqa: E402


class AudioServingTests(unittest.TestCase):
    def test_parse_standard_range(self):
        self.assertEqual(_parse_range_header("bytes=0-99", 1000), (0, 99))

    def test_parse_open_ended_range(self):
        self.assertEqual(_parse_range_header("bytes=100-", 1000), (100, 999))

    def test_parse_suffix_range(self):
        self.assertEqual(_parse_range_header("bytes=-100", 1000), (900, 999))

    def test_parse_large_suffix_range(self):
        self.assertEqual(_parse_range_header("bytes=-2000", 1000), (0, 999))


if __name__ == "__main__":
    unittest.main()
