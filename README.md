# Adhancron

Adhancron is an adhan scheduler available as a Docker home-server service and a native desktop application. It keeps daily prayer times up to date, serves the adhan MP3 over HTTP, and plays it through either Home Assistant or a Google Cast speaker directly.

The main supported use case is running this as a CasaOS or Docker container on a home server, then playing adhan through Home Assistant speakers such as Google Cast speakers.

## Editions

### Home Server: Docker and CasaOS

The Docker edition is designed for an always-on machine. It runs cron inside the container, so scheduled adhans continue even when no one has the dashboard open.

### Desktop: macOS and Windows

The desktop edition is an optional companion for people who prefer a native application instead of Docker. It opens the same dashboard in a native window, stores settings in the user's application-data folder, and uses an internal cross-platform scheduler instead of changing the machine's crontab.

Build the desktop edition on the target operating system:

```bash
python -m pip install -r desktop/requirements.txt
pyinstaller desktop/Adhancron.spec
```

On macOS this creates `dist/Adhancron.app`; on Windows it creates a `dist/Adhancron` application folder. The desktop app must stay open for scheduled adhans. See [desktop/README.md](desktop/README.md) for development, firewall, code-signing, and packaging details.

### ESP32-S3 Prayer Clock

`firmware/esp32-s3-prayer-clock` is a standalone appliance edition for the
LCDWIKI ES3N28P board: an ESP32-S3 with a 240x320 display, speaker, and
microSD slot. It will calculate and display local prayer times, play the adhan
from its own speaker, and later offer network playback as an optional companion
feature. It is developed independently from the Docker and desktop editions;
see [firmware/esp32-s3-prayer-clock/README.md](firmware/esp32-s3-prayer-clock/README.md).

## What It Does

- Runs a web dashboard on port `8090`.
- Creates and maintains cron jobs for Fajr, Dhuhr, Asr, Maghrib, and Isha.
- Calculates prayer times locally from the saved home latitude and longitude.
- Lets you manually edit prayer times from the web UI.
- Lets you disable or re-enable individual prayer jobs.
- Supports manual overrides so auto-update does not overwrite a custom time.
- Saves Home Assistant or direct Google Cast settings from the web UI.
- Stores the Home Assistant long-lived access token in `/data`, not in the GitHub repo.
- Serves `adhan_final.mp3` at `/audio/adhan_final.mp3`.
- Supports byte-range and `HEAD` requests for Cast/media-player playback.
- Logs playback triggers and audio fetches for troubleshooting.
- Uses Home Assistant's `announce` playback and confirms playback by polling the speaker's state.
- Can bypass Home Assistant and send the MP3 directly to a Google Cast speaker by IP address or hostname.

Older ESP32, MAX7219, Wi-Fi, and hardware test files are still present from earlier experiments, but they are not required for the Docker/Home Assistant workflow.

## How It Works

Adhancron has four moving parts:

1. **FastAPI web UI**
   The dashboard runs on port `8090`. It shows the prayer cron jobs, lets you edit them, saves Home Assistant settings, exposes the MP3 file, and provides a **Play Adhan Now** test button.

2. **Cron scheduler**
   The container runs cron internally. Each prayer has a cron entry that calls `trigger_ha.py` with the public MP3 URL and volume.

3. **Location-based prayer-time updater**
   The dashboard saves the home latitude and longitude, then calculates a local timetable and stores it in `/data/prayer_times.json`. It uses Fajr 90 minutes before sunrise, Zuhr five minutes after solar noon, standard Asr, Maghrib one minute after sunset, and the Moonsighting Committee seasonal Isha adjustment. The solar-position library can differ from Alislam by up to two minutes. At `00:05` every day, cron runs `update_adhan.py`, checks that the calendar covers the current and following year, and updates the prayer cron entries. Manual overrides are preserved.

4. **Playback trigger**
   `trigger_ha.py` uses the saved playback method. With **Home Assistant**, it calls the REST API, sets volume, sends `media_player.play_media` with `announce: true`, and polls the speaker state until it reports `playing`. If the speaker never reaches a playing state (or the integration does not support `announce`), it falls back to a plain `play_media`. With **Direct Google Cast**, it connects directly to the configured Google Cast device on its local IP/hostname and port `8009`, sets volume, and sends it the public MP3 URL.

## Playback Flow

At prayer time:

1. Cron runs a command like:

   ```text
   cd /app && . /data/adhan.env && /usr/local/bin/python /app/trigger_ha.py http://YOUR_HOST:8090/audio/adhan_final.mp3 0.8
   ```

2. `trigger_ha.py` loads saved settings from `/data/adhan_settings.json`.

3. With Home Assistant selected, it contacts Home Assistant at the configured `HA_URL`, which tells the configured `media_player` entity to play the public audio URL using `announce: true`.

4. With Direct Google Cast selected, it contacts the Google Cast speaker directly at its configured IP address or hostname.

5. The speaker fetches the MP3 from Adhancron’s `/audio/adhan_final.mp3` endpoint.

6. Adhancron logs the playback trigger and the speaker’s audio request.

## Web UI

Open:

```text
http://YOUR_DOCKER_HOST_IP:8090
```

The dashboard includes:

- Prayer cards for Fajr, Dhuhr, Asr, Maghrib, and Isha.
- Time inputs for each prayer.
- A switch to enable or disable each cron job.
- A manual override switch to prevent auto-update from changing a custom time.
- A Home Assistant settings panel.
- A **Play Adhan Now** button.
- A visible app version badge.
- A Fajr auto-update status panel.

## Playback Settings

Choose one method in the web UI:

### Home Assistant

In the web UI, save:

- **Home Assistant URL**
  Example: `http://192.168.1.22:8123`

- **Speaker Entity**
  Example: `media_player.bedroom_speaker`

- **Access Token**
  A Home Assistant long-lived access token.

The automatically selected audio URL must be reachable by Home Assistant and the speaker. Avoid opening the dashboard through `localhost` on the machine that runs Docker; use that machine's LAN IP instead.

The token is saved to:

```text
/data/adhan_settings.json
```

The API does not echo the token back to the browser.

### Direct Google Cast

This does not need a Home Assistant URL, entity ID, or token. Save:

- **Google Cast Speaker IP or Hostname**
  Example: `192.168.1.24`
Use the speaker's stable LAN IP address where possible. You can find it in your router's device list or in the Google Home app's device information. The Google speaker must be able to reach the public audio URL directly.

Adhancron automatically uses the address you used to open its dashboard, for example `http://192.168.1.16:8090`. Only use **Advanced audio address** in the dashboard if that address is not reachable by the speaker.

Direct Google Cast starts a normal media session. It will interrupt existing audio on that speaker and does not restore previous playback afterwards. Choose Home Assistant when you specifically want its `announce` behavior or want to target Home Assistant speaker groups.

## Before You Install

You will need three things from your existing setup. Have them ready before you start:

1. **Home Assistant URL** — the address of your Home Assistant, including the port. Prefer the IP address over `homeassistant.local`, because the container often cannot resolve `.local` names.

   ```text
   http://192.168.1.22:8123
   ```

2. **Media player entity ID** — the speaker you want the adhan to play on. Find it in Home Assistant under **Developer Tools → States** (or **Settings → Devices & Services → Entities**) and filter for `media_player.`.

   ```text
   media_player.bedroom_speaker
   ```

3. **Long-lived access token** — in Home Assistant, click your **user profile** (bottom-left), scroll to **Long-Lived Access Tokens**, and choose **Create Token**. Copy it somewhere safe; Home Assistant only shows it once.

You will also need the **IP address of the machine that runs the container** (your CasaOS box or Docker host). Home Assistant and the speaker must be able to reach Adhancron at `http://THAT_IP:8090`, so `localhost` will not work here.

## CasaOS Install

The CasaOS template builds the Docker image directly from this GitHub repository.

1. Open the compose template and copy its contents:

   ```text
   https://github.com/drhammadkhan/Adhancron/blob/main/casaos/docker-compose.yml
   ```

2. In CasaOS, go to **App Store → Custom Install** (use Docker Compose mode if offered) and paste the compose text.

3. Edit these values in the pasted compose before installing:

   - For Home Assistant playback, set `HA_URL` and `HA_ENTITY_ID`.
   - For direct Google Cast playback, set `ADHAN_PLAYBACK_METHOD` to `google_cast` and `GOOGLE_CAST_HOST` to the speaker's LAN IP address. These can also be set later in the web UI.

   You can leave `HA_TOKEN` empty and save it later in the web UI (recommended), or paste it here.

4. Confirm the port mapping is `8090:8090` and the data volume maps to `/DATA/AppData/adhan-manager:/data`.

5. Install the app, then open:

   ```text
   http://YOUR_CASAOS_HOST_IP:8090
   ```

6. Complete the [First-Run Setup](#first-run-setup) below.

See `CASAOS.md` for a slower step-by-step CasaOS guide.

## Docker Compose Install

Clone the repo and create your environment file:

```bash
git clone https://github.com/drhammadkhan/Adhancron.git
cd Adhancron
cp .env.example .env
```

Edit `.env` with the values from **Before You Install**:

```text
HA_URL=http://YOUR_HOME_ASSISTANT_IP:8123
HA_ENTITY_ID=media_player.bedroom_speaker
ADHAN_PLAYBACK_METHOD=home_assistant
# For direct Google Cast instead: ADHAN_PLAYBACK_METHOD=google_cast
# GOOGLE_CAST_HOST=192.168.1.24
# Optional advanced override. Normally captured automatically from the dashboard URL.
PUBLIC_BASE_URL=
ADHAN_VOLUME=0.8
# Optional: you can paste the token here, or save it from the web UI later.
HA_TOKEN=
```

Build and start:

```bash
docker compose up -d --build
```

Open the dashboard, then complete the [First-Run Setup](#first-run-setup):

```text
http://YOUR_DOCKER_HOST_IP:8090
```

## Docker Run Install

```bash
docker build -t adhan-manager .
docker run -d \
  --name adhan-manager \
  --restart unless-stopped \
  -p 8090:8090 \
  -v adhan-data:/data \
  -e HA_URL="http://YOUR_HOME_ASSISTANT_IP:8123" \
  -e HA_ENTITY_ID="media_player.bedroom_speaker" \
  adhan-manager
```

Then open `http://YOUR_DOCKER_HOST_IP:8090` and complete the [First-Run Setup](#first-run-setup).

## First-Run Setup

However you installed, finish setup from the web dashboard at `http://YOUR_HOST_IP:8090`:

1. In the **Playback** panel, choose either **Home Assistant** or **Direct Google Cast**.
2. For **Home Assistant**, fill in:
   - **Home Assistant URL** (e.g. `http://192.168.1.22:8123`)
   - **Speaker Entity** (e.g. `media_player.bedroom_speaker`)
   - **Access Token** — paste your long-lived token here if you did not set `HA_TOKEN`.
   - **Latitude** and **Longitude** — enter these manually or choose **Use This Device's Location**.
3. For **Direct Google Cast**, fill in the speaker IP/hostname and leave the port at `8009`. The Home Assistant fields are not needed.
4. Click **Save Settings**. The playback settings and location are written to `/data/adhan_settings.json` (owner-only); the app calculates and saves the timetable at `/data/prayer_times.json`, then applies today's cron schedule.
5. Ensure the container `TZ` value matches your home timezone. Cron uses this timezone when it runs the saved local times.
6. Click **Play Adhan Now** to test. The speaker should play the adhan within a few seconds.
7. Check that the prayer cards show today's times. The schedule auto-updates daily at `00:05`; you can also edit any time manually and toggle a **Manual** override so the daily update leaves it alone.

If nothing plays, see [Troubleshooting](#troubleshooting) — the trigger log at `/data/adhan.log` will say whether playback was confirmed.

## Configuration

Environment variables:

| Variable | Purpose | Default |
| --- | --- | --- |
| `HA_URL` | Home Assistant base URL | `http://homeassistant.local:8123` |
| `HA_ENTITY_ID` | Target media player entity | `media_player.bedroom_speaker` |
| `HA_TOKEN` | Optional Home Assistant token | empty |
| `ADHAN_PLAYBACK_METHOD` | `home_assistant` or `google_cast` | `home_assistant` |
| `GOOGLE_CAST_HOST` | Google Cast speaker IP address or hostname | empty |
| `GOOGLE_CAST_PORT` | Advanced Google Cast control-port override | `8009` |
| `PUBLIC_BASE_URL` | Optional advanced audio URL override | automatic dashboard URL |
| `ADHAN_VOLUME` | Playback volume, `0.0` to `1.0` | `0.8` |
| `ADHAN_AUDIO_FILE` | Audio file served from `/audio` | `adhan_final.mp3` |
| `ADHAN_LATITUDE` | Home latitude when not saved through the web UI | empty |
| `ADHAN_LONGITUDE` | Home longitude when not saved through the web UI | empty |
| `ADHAN_USE_ANNOUNCE` | Use Home Assistant `announce: true` playback first | `true` |
| `ADHAN_PLAY_ATTEMPTS` | Max fallback `play_media` attempts when playback is not confirmed | `2` |
| `ADHAN_PLAYBACK_TIMEOUT` | Seconds to wait for the player to report `playing` | `15` |
| `ADHAN_CAST_CONNECT_TIMEOUT` | Seconds to wait for a direct Google Cast speaker connection | `30` |
| `ADHAN_PLAYBACK_POLL_INTERVAL` | Seconds between player state polls | `1` |
| `ADHAN_MEDIA_CONTENT_TYPE` | Media type sent to Home Assistant | `audio/mpeg` |
| `TZ` | Container timezone | `Europe/London` |

Settings saved from the web UI take priority for the playback method, Home Assistant URL/entity/token, Google Cast host/port, audio URL override, latitude, and longitude.

## Persistent Data

The `/data` volume contains:

| File | Purpose |
| --- | --- |
| `adhan.env` | Environment file sourced by cron jobs |
| `adhan.log` | Playback trigger logs from prayer cron jobs |
| `adhan_access.log` | Audio endpoint access logs from speakers/Cast devices |
| `adhan_update.log` | Daily updater output |
| `adhan_settings.json` | Saved Home Assistant settings and token |
| `adhan_overrides.json` | Manual override flags |
| `adhan_update_status.json` | Last updater status shown in the UI |
| `prayer_times.json` | Locally calculated timetable for the saved location |

Do not commit files from `/data`. They may contain local configuration or secrets.

## Audio Serving

The app serves:

```text
http://YOUR_DOCKER_HOST_IP:8090/audio/adhan_final.mp3
```

The endpoint supports:

- `GET`
- `HEAD`
- `Accept-Ranges: bytes`
- `206 Partial Content`
- suffix ranges such as `Range: bytes=-100`
- `Content-Type: audio/mpeg`

This matters because Cast devices and media players may stream audio in chunks rather than downloading the whole file in one simple request.

The included MP3 has been encoded in a Cast-friendly format:

```text
MP3, 128 kbps, 44.1 kHz, stereo, ID3v2.3
```

## Version Check

The app exposes its runtime version at:

```text
http://YOUR_DOCKER_HOST_IP:8090/api/version
```

You can also check inside the container:

```bash
docker exec -it adhan-manager cat /app/VERSION
```

And inspect the image label:

```bash
docker inspect adhan-manager --format '{{ index .Config.Labels "org.opencontainers.image.version" }}'
```

## Useful Commands

Show running logs:

```bash
docker logs -f adhan-manager
```

Show prayer cron jobs:

```bash
docker exec -it adhan-manager crontab -l
```

Force a schedule refresh:

```bash
docker exec -it adhan-manager python /app/update_adhan.py
```

Trigger playback manually from inside the container:

```bash
docker exec -it adhan-manager python /app/trigger_ha.py "http://YOUR_DOCKER_HOST_IP:8090/audio/adhan_final.mp3" 0.8
```

Check trigger logs:

```bash
docker exec -it adhan-manager tail -100 /data/adhan.log
```

Check whether the speaker fetched the MP3:

```bash
docker exec -it adhan-manager tail -100 /data/adhan_access.log
```

## Troubleshooting

### Play Adhan Now Works, But Timed Prayer Does Not

Check the crontab:

```bash
docker exec -it adhan-manager crontab -l
```

Each prayer should use the normalized command shape:

```text
cd /app && . /data/adhan.env && /usr/local/bin/python /app/trigger_ha.py http://YOUR_HOST:8090/audio/adhan_final.mp3 0.8 >> /data/adhan.log 2>&1
```

If not, rebuild the container and run:

```bash
docker exec -it adhan-manager python /app/update_adhan.py
```

You can also click **Save Settings** in the web UI to repair existing cron commands.

### Speaker Goes Bing But No Adhan Plays

This means Home Assistant reached the speaker, but the media session may not have completed playback.

First check the trigger log to see what playback confirmation reported:

```bash
docker exec -it adhan-manager tail -100 /data/adhan.log
```

`trigger_ha.py` logs whether playback was confirmed:

- `Trigger complete: playback confirmed (announce)` — the speaker reported a playing state. If you still heard nothing, the issue is downstream of Home Assistant (speaker volume, the speaker could not reach the audio URL, or a Cast app problem).
- `Trigger complete: playback could not be confirmed` — the speaker never reported `playing` within `ADHAN_PLAYBACK_TIMEOUT`. Confirm the speaker entity is correct and that the automatically selected dashboard address is reachable from the speaker.

If your integration does not support `announce`, set `ADHAN_USE_ANNOUNCE=false` to use plain `play_media` confirmed by state polling.

You can also check whether the speaker fetched the file at all:

```bash
docker exec -it adhan-manager tail -100 /data/adhan_access.log
```

Note that Cast devices stream in chunks via range requests, so a single request showing `"completed": false` is normal and not by itself a failure — rely on the trigger log's confirmation line instead.

### Direct Google Cast Cannot Connect

Check that the saved Google Cast IP address is the current address of the speaker and that the CasaOS host can reach port `8009` on it. Give the speaker a DHCP reservation in your router so its address remains stable. The speaker, the CasaOS server, and the public audio URL must all be on reachable network paths.

### Home Assistant URL Does Not Resolve

Inside Docker, `homeassistant.local` may not resolve. Use the Home Assistant IP address instead:

```text
http://192.168.1.22:8123
```

Save that in the web UI.

### Token Does Not Appear In CasaOS

The token saved in the web UI is not written back into CasaOS environment variables. It is saved in:

```text
/data/adhan_settings.json
```

The web UI should show whether the token source is saved settings, environment, or missing.

### Audio URL Is Wrong

Adhancron normally captures the dashboard address automatically. If the speaker cannot reach it, open **Advanced audio address** and set an override that Home Assistant and the speaker can reach:

```text
http://YOUR_DOCKER_HOST_IP:8090
```

The prayer cards should then show:

```text
http://YOUR_DOCKER_HOST_IP:8090/audio/adhan_final.mp3 • volume 0.8
```

## Project Layout

| Path | Purpose |
| --- | --- |
| `adhan_web_ui/app.py` | FastAPI app, dashboard API, audio endpoint |
| `adhan_web_ui/static/` | Web UI frontend and icon |
| `adhan_web_ui/cron_manager.py` | Crontab parser and writer |
| `adhan_web_ui/settings_manager.py` | Secure settings persistence |
| `adhan_web_ui/override_manager.py` | Manual override persistence |
| `adhan_config.py` | Shared runtime paths and command builder |
| `trigger_ha.py` | Home Assistant playback trigger |
| `apply_adhan_cron.py` | Prayer schedule updater |
| `update_adhan.py` | Wrapper used by cron/startup |
| `prayer_times.json` | Prayer time data |
| `adhan_final.mp3` | Default adhan audio |
| `Dockerfile` | Container image |
| `docker-entrypoint.sh` | Starts updater, cron, and web UI |
| `docker-compose.yml` | Generic Docker Compose file |
| `casaos/docker-compose.yml` | CasaOS compose template |
| `casaos/adhan.jpg` | CasaOS app icon |
| `VERSION` | Runtime version marker |

## Security Notes

- Do not commit Home Assistant tokens.
- Use the web UI to save the token into `/data`.
- `adhan_settings.json` is written with owner-only permissions.
- Avoid sharing logs that contain local network details if that matters for your setup.

## Development Checks

Run tests:

```bash
python3 -m unittest discover -s adhan_web_ui/tests
```

Validate CasaOS compose:

```bash
docker compose -f casaos/docker-compose.yml config
```

Compile Python files:

```bash
python3 -m compileall adhan_config.py trigger_ha.py apply_adhan_cron.py adhan_web_ui
```
