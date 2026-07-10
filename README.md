# Adhancron

Adhancron is a Dockerized adhan scheduler for Home Assistant. It keeps daily prayer-time cron jobs up to date, serves the adhan MP3 over HTTP, and asks Home Assistant to play that audio on a configured media player.

The main supported use case is running this as a CasaOS or Docker container on a home server, then playing adhan through Home Assistant speakers such as Google Cast speakers.

## What It Does

- Runs a web dashboard on port `8090`.
- Creates and maintains cron jobs for Fajr, Dhuhr, Asr, Maghrib, and Isha.
- Generates official Alislam prayer times from the saved home latitude and longitude.
- Lets you manually edit prayer times from the web UI.
- Lets you disable or re-enable individual prayer jobs.
- Supports manual overrides so auto-update does not overwrite a custom time.
- Saves Home Assistant settings from the web UI.
- Stores the Home Assistant long-lived access token in `/data`, not in the GitHub repo.
- Serves `adhan_final.mp3` at `/audio/adhan_final.mp3`.
- Supports byte-range and `HEAD` requests for Cast/media-player playback.
- Logs playback triggers and audio fetches for troubleshooting.
- Uses Home Assistant's `announce` playback and confirms playback by polling the speaker's state.

Older ESP32, MAX7219, Wi-Fi, and hardware test files are still present from earlier experiments, but they are not required for the Docker/Home Assistant workflow.

## How It Works

Adhancron has four moving parts:

1. **FastAPI web UI**
   The dashboard runs on port `8090`. It shows the prayer cron jobs, lets you edit them, saves Home Assistant settings, exposes the MP3 file, and provides a **Play Adhan Now** test button.

2. **Cron scheduler**
   The container runs cron internally. Each prayer has a cron entry that calls `trigger_ha.py` with the public MP3 URL and volume.

3. **Location-based prayer-time updater**
   The dashboard saves the home latitude and longitude, then asks Alislam for the official monthly timetable for that location and stores it in `/data/prayer_times.json`. At `00:05` every day, cron runs `update_adhan.py`, checks that the calendar covers the current and following year, and updates the prayer cron entries. Manual overrides are preserved.

4. **Home Assistant trigger**
   `trigger_ha.py` calls the Home Assistant REST API. It sets the speaker volume, then sends `media_player.play_media` with `announce: true` — Home Assistant's purpose-built "play this sound now" mode, which wakes the device, plays once, and restores prior state. It then polls the media player's state until it reports `playing`. If the speaker never reaches a playing state (or the integration does not support `announce`), it falls back to a plain `play_media` and re-sends only while playback remains unconfirmed.

## Playback Flow

At prayer time:

1. Cron runs a command like:

   ```text
   cd /app && . /data/adhan.env && /usr/local/bin/python /app/trigger_ha.py http://YOUR_HOST:8090/audio/adhan_final.mp3 0.8
   ```

2. `trigger_ha.py` loads saved settings from `/data/adhan_settings.json`.

3. It contacts Home Assistant at the configured `HA_URL`.

4. Home Assistant tells the configured `media_player` entity to play the public audio URL using `announce: true`.

5. The speaker or Cast device fetches the MP3 from Adhancron’s `/audio/adhan_final.mp3` endpoint.

6. `trigger_ha.py` polls the media player's state to confirm it actually started playing, and only re-sends the command if it did not.

7. Adhancron logs both the Home Assistant trigger and the speaker’s audio request.

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

## Home Assistant Settings

In the web UI, save:

- **Home Assistant URL**
  Example: `http://192.168.1.22:8123`

- **Speaker Entity**
  Example: `media_player.bedroom_speaker`

- **Public Audio URL**
  Example: `http://192.168.1.16:8090`

- **Access Token**
  A Home Assistant long-lived access token.

The public audio URL must be reachable by Home Assistant and by the speaker integration. Avoid `localhost` here unless Home Assistant is running inside the same container, which is not the normal setup.

The token is saved to:

```text
/data/adhan_settings.json
```

The API does not echo the token back to the browser.

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

   - `PUBLIC_BASE_URL` → `http://YOUR_CASAOS_HOST_IP:8090` (the template ships with a placeholder IP — you must change it).
   - `HA_URL` → your Home Assistant address, e.g. `http://192.168.1.22:8123`.
   - `HA_ENTITY_ID` → your speaker entity, e.g. `media_player.bedroom_speaker`.

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
PUBLIC_BASE_URL=http://YOUR_DOCKER_HOST_IP:8090
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
  -e PUBLIC_BASE_URL="http://YOUR_DOCKER_HOST_IP:8090" \
  adhan-manager
```

Then open `http://YOUR_DOCKER_HOST_IP:8090` and complete the [First-Run Setup](#first-run-setup).

## First-Run Setup

However you installed, finish setup from the web dashboard at `http://YOUR_HOST_IP:8090`:

1. In the **Home Assistant settings** panel, confirm or fill in:
   - **Home Assistant URL** (e.g. `http://192.168.1.22:8123`)
   - **Speaker Entity** (e.g. `media_player.bedroom_speaker`)
   - **Public Audio URL** (e.g. `http://YOUR_HOST_IP:8090`)
   - **Access Token** — paste your long-lived token here if you did not set `HA_TOKEN`.
   - **Latitude** and **Longitude** — enter these manually or choose **Use This Device's Location**.
2. Click **Save Settings**. The token and location are written to `/data/adhan_settings.json` (owner-only); the app downloads and saves the official timetable at `/data/prayer_times.json`, then applies today's cron schedule.
3. Ensure the container `TZ` value matches your home timezone. Cron uses this timezone when it runs the saved local times.
4. Click **Play Adhan Now** to test. The speaker should play the adhan within a few seconds.
5. Check that the prayer cards show today's times. The schedule auto-updates daily at `00:05`; you can also edit any time manually and toggle a **Manual** override so the daily update leaves it alone.

If nothing plays, see [Troubleshooting](#troubleshooting) — the trigger log at `/data/adhan.log` will say whether playback was confirmed.

## Configuration

Environment variables:

| Variable | Purpose | Default |
| --- | --- | --- |
| `HA_URL` | Home Assistant base URL | `http://homeassistant.local:8123` |
| `HA_ENTITY_ID` | Target media player entity | `media_player.bedroom_speaker` |
| `HA_TOKEN` | Optional Home Assistant token | empty |
| `PUBLIC_BASE_URL` | Base URL where HA/speaker can reach Adhancron | empty |
| `ADHAN_VOLUME` | Playback volume, `0.0` to `1.0` | `0.8` |
| `ADHAN_AUDIO_FILE` | Audio file served from `/audio` | `adhan_final.mp3` |
| `ADHAN_LATITUDE` | Home latitude when not saved through the web UI | empty |
| `ADHAN_LONGITUDE` | Home longitude when not saved through the web UI | empty |
| `ADHAN_USE_ANNOUNCE` | Use Home Assistant `announce: true` playback first | `true` |
| `ADHAN_PLAY_ATTEMPTS` | Max fallback `play_media` attempts when playback is not confirmed | `2` |
| `ADHAN_PLAYBACK_TIMEOUT` | Seconds to wait for the player to report `playing` | `15` |
| `ADHAN_PLAYBACK_POLL_INTERVAL` | Seconds between player state polls | `1` |
| `ADHAN_MEDIA_CONTENT_TYPE` | Media type sent to Home Assistant | `audio/mpeg` |
| `TZ` | Container timezone | `Europe/London` |

Settings saved from the web UI take priority for Home Assistant URL, entity, token, public audio URL, latitude, and longitude.

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
| `prayer_times.json` | Alislam timetable generated for the saved location |

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
- `Trigger complete: playback could not be confirmed` — the speaker never reported `playing` within `ADHAN_PLAYBACK_TIMEOUT`. Confirm the speaker entity is correct and that the **Public Audio URL** is reachable from the speaker.

If your integration does not support `announce`, set `ADHAN_USE_ANNOUNCE=false` to use plain `play_media` confirmed by state polling.

You can also check whether the speaker fetched the file at all:

```bash
docker exec -it adhan-manager tail -100 /data/adhan_access.log
```

Note that Cast devices stream in chunks via range requests, so a single request showing `"completed": false` is normal and not by itself a failure — rely on the trigger log's confirmation line instead.

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

### Public Audio URL Is Wrong

Set **Public Audio URL** to the address Home Assistant and the speaker can reach:

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
