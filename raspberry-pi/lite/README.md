# Adhan Clock Lite For Raspberry Pi

This is the recommended Raspberry Pi 3B installation. It runs the complete
Adhancron application on Raspberry Pi OS Lite without Docker, a desktop
environment, or Chromium.

The native Python service calculates and schedules prayer times. Nginx exposes
the dashboard at `http://adhanclock.local`, and the lightweight Cog browser
draws the HDMI prayer clock directly through Linux DRM. Cage/Wayland is an
automatic compatibility fallback.

## Install

1. In Raspberry Pi Imager, choose **Raspberry Pi OS Lite (64-bit)**.
2. In Imager settings, enter Wi-Fi, create a user, enable SSH, and select the
   correct locale and time zone.
3. Boot the Pi and connect over SSH.
4. Run:

   ```bash
   sudo apt update
   sudo apt install -y git
   git clone https://github.com/drhammadkhan/Adhancron.git
   cd Adhancron
   sudo ./raspberry-pi/lite/install.sh
   sudo reboot
   ```

After reboot, the HDMI clock opens automatically. From another device on the
same network, visit `http://adhanclock.local`. Enter a town, postcode, or
coordinates, then choose attached audio or a network speaker.

## Why It Is Lean

- No desktop operating system.
- No Docker daemon or container filesystem.
- No Chromium process.
- One native FastAPI service, Nginx, and Cog.
- Direct DRM output when supported.

This leaves substantially more CPU time and memory available on a Pi 3B while
retaining the same dashboard, local calculation, scheduler, audio files, Eid
takbeer, Ramadan features, and speaker integrations.

## Updates And Rollback

`adhancron-native-update.timer` checks GitHub twice a day. An update is unpacked
into a new versioned directory and receives its own Python environment. Only
after that succeeds are the `current` links switched.

The service is restarted and health checked. A failed health check immediately
restores the previous application and Python environment. Settings and
generated timetables remain separately in `/opt/adhancron/data`.

Check or run updates:

```bash
systemctl status adhancron-native-update.timer
sudo systemctl start adhancron-native-update.service
journalctl -u adhancron-native-update.service -n 100
```

Disable automatic updates:

```bash
sudo systemctl disable --now adhancron-native-update.timer
```

Updates track the repository's `main` branch over HTTPS. They are health
checked and reversible, but are not code-signed packages.

## Operations

```bash
# Application and display status
systemctl status adhancron adhanclock-display

# Recent logs
journalctl -u adhancron -n 100
journalctl -u adhanclock-display -n 100
tail -100 /opt/adhancron/data/adhan.log

# Restart
sudo systemctl restart adhancron adhanclock-display
```

Attached audio uses `mpg123` and ALSA. Change `ADHAN_ALSA_DEVICE` in
`/etc/adhancron/adhancron.env` if `default` is not the desired HDMI or analogue
output, then restart `adhancron`.

## Important Paths

| Path | Purpose |
| --- | --- |
| `/opt/adhancron/current` | Active application release |
| `/opt/adhancron/venv` | Active Python environment |
| `/opt/adhancron/releases` | Versioned application releases |
| `/opt/adhancron/venvs` | Versioned Python environments |
| `/opt/adhancron/data` | Persistent settings, schedules, recordings, and logs |
| `/etc/adhancron/adhancron.env` | Appliance-level defaults |
| `/opt/adhancron/source` | Git checkout used by the updater |
