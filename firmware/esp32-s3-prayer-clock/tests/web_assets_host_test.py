import pathlib
import unittest
from html.parser import HTMLParser


ROOT = pathlib.Path(__file__).resolve().parents[1]
WEB = ROOT / "main" / "web"


class PageParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []
        self.names = []
        self.stylesheets = []
        self.scripts = []

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if values.get("id"):
            self.ids.append(values["id"])
        if values.get("name"):
            self.names.append(values["name"])
        if tag == "link" and values.get("rel") == "stylesheet":
            self.stylesheets.append(values.get("href"))
        if tag == "script" and values.get("src"):
            self.scripts.append(values["src"])


def parse_page(name):
    parser = PageParser()
    parser.feed((WEB / name).read_text())
    return parser


class WebAssetsTest(unittest.TestCase):
    def test_assets_are_small_enough_for_embedded_serving(self):
        for path in WEB.iterdir():
            self.assertLess(path.stat().st_size, 24 * 1024, path.name)

    def test_pages_use_only_embedded_styles_and_scripts(self):
        for name in ("index.html", "wifi.html", "location.html"):
            page = parse_page(name)
            self.assertEqual(page.stylesheets, ["/ui.css"])
            self.assertEqual(page.scripts, ["/ui.js"])

    def test_ids_are_unique_on_each_page(self):
        for name in ("index.html", "wifi.html", "location.html"):
            ids = parse_page(name).ids
            self.assertEqual(len(ids), len(set(ids)), name)

    def test_dashboard_posts_every_saved_setting(self):
        names = set(parse_page("index.html").names)
        expected = {
            "step", "output", "cast_device_id", "cast_device_name",
            "dlna_device_url", "dlna_device_name", "volume", "fajr",
            "dhuhr", "asr", "maghrib", "isha", "ramadan_start",
            "ramadan_end", "eid_fitr", "eid_adha", "eid_takbeer_start",
            "eid_takbeer_end", "eid_takbeer_interval", "automatic_updates",
            "display_style",
        }
        self.assertTrue(expected.issubset(names))


if __name__ == "__main__":
    unittest.main()
