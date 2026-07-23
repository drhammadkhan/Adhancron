# Raspberry Pi HDMI Prayer Clock

Adhancron has two Raspberry Pi installation profiles. Both turn a Pi and an
HDMI display into a full-screen Adhan Clock using the same locally calculated
timetable, scheduler, dashboard, recordings, Ramadan and Eid features, and
speaker integrations as the other editions.

## Choose An Edition

### Lite Appliance (recommended for Pi 3B)

Use [the Lite installer](lite/README.md) with **Raspberry Pi OS Lite
(64-bit)**. It runs Python natively and renders the display with Cog directly
through DRM. There is no desktop environment, Docker daemon, or Chromium, so it
boots faster and leaves substantially more memory available.

```bash
sudo ./raspberry-pi/lite/install.sh
```

### Docker + Desktop (compatibility edition)

The installer documented below uses Raspberry Pi OS Desktop, Docker, and
Chromium. Choose it when you specifically want container isolation or need the
desktop for other Pi applications.

## What You Need

- Raspberry Pi 3B, 3B+, 4, or 5.
- A microSD card of at least 16 GB.
- Raspberry Pi OS Desktop **64-bit**. The desktop image is required for the full-screen Chromium display.
- An HDMI display. The interface adapts to other landscape resolutions, but is designed and tested at 800x480.
- A reliable Raspberry Pi power supply.
- Internet access during installation.

Audio can play through the Pi's attached HDMI or analogue audio device, or through Home Assistant, Google Cast, AirPlay, or Sonos/UPnP-DLNA.

## Install

1. Use Raspberry Pi Imager to install **Raspberry Pi OS with desktop (64-bit)**. In the Imager settings, configure Wi-Fi, a username, and password.
2. Boot the Pi, open Terminal, and clone this repository:

   ```bash
   sudo apt update
   sudo apt install -y git
   git clone https://github.com/drhammadkhan/Adhancron.git
   cd Adhancron
   ```

3. Run the installer:

   ```bash
   sudo ./raspberry-pi/install.sh
   ```

4. Reboot when the installer finishes:

   ```bash
   sudo reboot
   ```

The Pi opens the prayer clock automatically after desktop login. On a phone or computer connected to the same network, open:

```text
http://adhanclock.local
```

Save a location and select where the adhan should play. The clock then calculates the current and following year's prayer times locally and schedules all five daily adhans.

The installer inherits Raspberry Pi OS's time zone, including daylight-saving changes. Set the Pi's region and time zone correctly in Raspberry Pi Imager or **Preferences > Raspberry Pi Configuration**; the prayer-location search itself needs only a town, postcode, or coordinates.

## Attached Audio

Choose **Attached / HDMI speaker** in the dashboard to play directly from the Pi. The default ALSA device is `default`. If the wrong output is used:

1. Open Terminal on the Pi and list playback devices:

   ```bash
   aplay -L
   ```

2. Edit `/opt/adhancron/adhancron.env` and change `ADHAN_ALSA_DEVICE`. Common values include `default`, `sysdefault`, or a `plughw:` device shown by `aplay -L`.
3. Restart the service:

   ```bash
   cd /opt/adhancron
   sudo docker compose --env-file adhancron.env up -d
   ```

The **Play Adhan Now** button is the quickest way to test the selected output.

## Clock Faces

The dashboard's **Large display** setting offers two views:

- **Overview** shows the current time, next prayer, countdown, and all five prayer times.
- **Focus** gives the next prayer more visual prominence while retaining the daily schedule.

Changes appear on the HDMI display within 30 seconds. Open `http://adhanclock.local/display` to preview the clock in any browser.

## Automatic Updates

The installer enables `adhancron-update.timer`. Twice a day it pulls the latest signed-in-public GitHub Container Registry image and recreates only the application container. Settings, schedules, logs, uploaded takbeer audio, and generated prayer times remain in `/opt/adhancron/data`.

Check the timer:

```bash
systemctl status adhancron-update.timer
```

Install an update immediately:

```bash
sudo /opt/adhancron/update.sh
```

Disable automatic application updates:

```bash
sudo systemctl disable --now adhancron-update.timer
```

## Useful Controls

Exit the full-screen browser with `Alt+F4`. Start it again by logging out and back in, rebooting, or running:

```bash
~/.local/bin/adhancron-kiosk
```

View application logs:

```bash
docker logs --tail 100 adhan-manager
tail -100 /opt/adhancron/data/adhan.log
```

Restart the application:

```bash
sudo systemctl restart docker
cd /opt/adhancron
sudo docker compose --env-file adhancron.env up -d
```

The dashboard remains available even if Chromium is closed. If `adhanclock.local` does not resolve on a particular phone, use the Pi's IP address, shown by `hostname -I`.

## Files And Services

| Path | Purpose |
| --- | --- |
| `/opt/adhancron/data` | Persistent user settings, timetables, logs, and custom takbeer audio |
| `/opt/adhancron/adhancron.env` | Pi-specific defaults such as attached audio device |
| `/opt/adhancron/docker-compose.yml` | ARM64 application service |
| `~/.local/bin/adhancron-kiosk` | Full-screen display launcher |
| `/etc/nginx/sites-enabled/adhanclock` | Port 80 dashboard proxy |
| `adhancron-update.timer` | Twice-daily image update check |

The installer does not modify Adhancron settings during an update and never stores Home Assistant secrets in this repository.
