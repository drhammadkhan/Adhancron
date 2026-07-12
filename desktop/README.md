# Adhancron Desktop

The desktop edition uses the same dashboard, prayer-time calculator, Home Assistant integration, and direct Google Cast playback as the Docker edition.

## Run for development

```bash
python -m pip install -r desktop/requirements.txt
python -m desktop.launcher
```

The app opens in a native window. Its settings and schedule are stored outside the source tree:

- macOS: `~/Library/Application Support/Adhancron`
- Windows: `%APPDATA%\\Adhancron`

The desktop window must remain open for scheduled adhans. It deliberately uses an internal scheduler rather than modifying the machine's crontab.

## Build a native installer

Build separately on each target operating system:

```bash
pyinstaller desktop/Adhancron.spec
```

This produces an application bundle in `dist/`. macOS distribution requires Apple code signing and notarization; Windows distribution should be code signed to avoid SmartScreen warnings.

## Local-network playback

For Google Cast playback, the desktop app automatically uses the computer's LAN address to serve the audio file. Allow Adhancron through the operating system firewall when prompted so the speaker can reach it.
