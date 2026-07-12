from __future__ import annotations

import os
from pathlib import Path
import socket
import sys
import threading
import time
import webbrowser


def _system_timezone() -> str:
    """Return the IANA timezone configured by the host operating system."""
    localtime_path = os.path.realpath("/etc/localtime")
    marker = "/zoneinfo/"
    if marker in localtime_path:
        return localtime_path.split(marker, 1)[1]

    try:
        from tzlocal import get_localzone_name

        return get_localzone_name()
    except Exception:
        return "UTC"


def _bundle_dir() -> Path:
    if getattr(sys, "frozen", False):
        if sys.platform == "darwin":
            # PyInstaller places bundled assets in Contents/Resources on macOS.
            return Path(sys.executable).resolve().parents[1] / "Resources"
        return Path(sys._MEIPASS)  # type: ignore[attr-defined]
    return Path(__file__).resolve().parents[1]


def _data_dir() -> Path:
    home = Path.home()
    if sys.platform == "win32":
        base = Path(os.getenv("APPDATA", home / "AppData" / "Roaming"))
    elif sys.platform == "darwin":
        base = home / "Library" / "Application Support"
    else:
        base = Path(os.getenv("XDG_DATA_HOME", home / ".local" / "share"))
    return base / "Adhancron"


def _lan_address() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("8.8.8.8", 80))
            return probe.getsockname()[0]
    except OSError:
        return "127.0.0.1"


def _configure_environment(port: int) -> None:
    app_dir = _bundle_dir()
    data_dir = _data_dir()
    data_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("ADHAN_APP_DIR", str(app_dir))
    os.environ.setdefault("ADHAN_DATA_DIR", str(data_dir))
    os.environ.setdefault("ADHAN_RUNTIME", "desktop")
    os.environ.setdefault("PUBLIC_BASE_URL", f"http://{_lan_address()}:{port}")
    os.environ.setdefault("TZ", _system_timezone())
    if hasattr(time, "tzset"):
        time.tzset()


def run(port: int = 8090) -> None:
    _configure_environment(port)

    import uvicorn
    from adhan_web_ui.app import app
    from desktop.scheduler import DesktopScheduler

    scheduler = DesktopScheduler()
    scheduler.start()
    server = uvicorn.Server(uvicorn.Config(app, host="0.0.0.0", port=port, log_level="warning"))
    thread = threading.Thread(target=server.run, name="adhancron-web", daemon=True)
    thread.start()

    url = f"http://127.0.0.1:{port}"
    for _ in range(100):
        if server.started:
            break
        time.sleep(0.05)

    try:
        import webview
        window = webview.create_window("Adhancron", url, width=1240, height=900, min_size=(900, 650))
        webview.start()
        del window
    except ImportError:
        webbrowser.open(url)
        thread.join()
    finally:
        scheduler.stop()
        server.should_exit = True


if __name__ == "__main__":
    run()
