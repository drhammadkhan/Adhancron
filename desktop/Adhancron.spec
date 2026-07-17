# Build this file on the target operating system with PyInstaller.
# pyinstaller desktop/Adhancron.spec
from pathlib import Path
import sys
from PyInstaller.utils.hooks import collect_data_files, collect_submodules

ROOT = Path(SPECPATH).parent
datas = [
    (str(ROOT / "adhan_web_ui" / "static"), "adhan_web_ui/static"),
    (str(ROOT / "adhan_final.mp3"), "."),
    (str(ROOT / "eid_takbeer.mp3"), "."),
    (str(ROOT / "VERSION"), "."),
] + collect_data_files("pyatv")

hiddenimports = [
    "uvicorn.logging",
    "uvicorn.loops.auto",
    "uvicorn.protocols.http.auto",
    "webview.platforms.cocoa",
    "webview.platforms.winforms",
    "timezonefinder",
] + collect_submodules("pyatv")

a = Analysis(
    [str(ROOT / "desktop" / "launcher.py")],
    pathex=[str(ROOT)],
    datas=datas,
    hiddenimports=hiddenimports,
)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name="Adhancron", console=False)
if sys.platform == "darwin":
    app = BUNDLE(
        exe,
        a.binaries,
        a.zipfiles,
        a.datas,
        name="Adhancron.app",
        bundle_identifier="org.adhancron.desktop",
    )
else:
    app = COLLECT(exe, a.binaries, a.zipfiles, a.datas, name="Adhancron")
