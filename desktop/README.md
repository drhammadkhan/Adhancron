# Adhancron Desktop

The desktop edition uses the same dashboard, prayer-time calculator, Home
Assistant integration, Google Cast, AirPlay, and Sonos/UPnP-DLNA playback as
the Docker edition.

## Run for development

```bash
python -m pip install -r desktop/requirements.txt
python -m desktop.launcher
```

The app opens in a native window. Its settings and schedule are stored outside the source tree:

- macOS: `~/Library/Application Support/Adhancron`
- Windows: `%APPDATA%\\Adhancron`

The desktop window must remain open for scheduled adhans. It deliberately uses an internal scheduler rather than modifying the machine's crontab.

The desktop build also includes the complete Eid feature: separate Eid al-Fitr
and Eid al-Adha dates, an all-day greeting, a ten-minute next-takbeer countdown,
and repeated takbeer playback during the configured window. Normal prayer
adhans remain enabled. The bundled recording can be tested or replaced from
Settings; a replacement is stored in the application-data folder and survives
application upgrades.

## Build a native installer

Build separately on each target operating system:

```bash
pyinstaller desktop/Adhancron.spec
```

This produces an application bundle in `dist/`. macOS distribution requires Apple code signing and notarization; Windows distribution should be code signed to avoid SmartScreen warnings.

GitHub Actions produces separate `Adhancron-macOS-Apple-Silicon` and `Adhancron-macOS-Intel` artifacts. Download the artifact that matches the Mac's processor. Until the project is signed and notarized with an Apple Developer ID, macOS will require the first launch to use Control-click **Open** from Finder.

## Local-network playback

For Google Cast, AirPlay, and Sonos/DLNA playback, the desktop app
automatically uses the computer's LAN address to serve the adhan or takbeer
audio file. Allow Adhancron through the operating system firewall when prompted
so the speaker can reach it. AirPlay pairing credentials are stored alongside
the rest of the user's Adhancron application data.
