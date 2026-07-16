# Docker Deployment

This container runs the Home Assistant adhan scheduler and web UI.

## Required Configuration

- `HA_URL`: Home Assistant base URL, for example `http://homeassistant.local:8123`.
- `HA_ENTITY_ID`: target media player, for example `media_player.bedroom_speaker`.
- `PUBLIC_BASE_URL`: URL that Home Assistant can use to fetch this container's audio endpoint, for example `http://YOUR_DOCKER_HOST_IP:8090`.

The Home Assistant long-lived access token can be saved from the web UI after the container starts. You can also provide `HA_TOKEN` as an environment variable if you prefer.

`PUBLIC_BASE_URL` must be reachable from Home Assistant and the speaker integration. The container serves the adhan file at:

```text
${PUBLIC_BASE_URL}/audio/adhan_final.mp3
```

It also bundles and serves the separate Eid takbeer at
`${PUBLIC_BASE_URL}/audio/eid_takbeer.mp3`. Configure the two locally observed
Eid dates and takbeer window from the dashboard; no additional environment
variables are required.

## Build

```bash
docker build -t adhan-manager .
```

## Run

With Compose:

```bash
docker compose up -d --build
```

With `docker run`:

```bash
docker run -d \
  --name adhan-manager \
  --restart unless-stopped \
  -p 8090:8090 \
  -v adhan-data:/data \
  -e HA_URL="http://homeassistant.local:8123" \
  -e HA_ENTITY_ID="media_player.bedroom_speaker" \
  -e PUBLIC_BASE_URL="http://YOUR_DOCKER_HOST_IP:8090" \
  adhan-manager
```

The web UI will be available at:

```text
http://localhost:8090
```

## What Starts Inside The Container

- A cron daemon for prayer-time jobs.
- A daily updater at `00:05` that refreshes today's prayer times from `prayer_times.json`.
- A once-per-minute Eid dispatcher that only acts on configured Eid dates and slots.
- A FastAPI web UI on port `8090`.
- An `/audio/{filename}` endpoint with byte-range support for speaker playback.

## Persistent Data

The `/data` volume stores:

- `adhan.log`: playback cron output.
- `adhan_update.log`: daily schedule update output.
- `adhan_update_status.json`: status displayed by the web UI.
- `adhan_overrides.json`: manual override flags from the web UI.
- `adhan_settings.json`: Home Assistant URL, entity, and saved access token. This file is written with owner-only permissions.
- `eid_takbeer.log`: Eid scheduler and playback output.
- `eid_takbeer_state.json`: last claimed Eid slot for duplicate prevention.
- `eid_takbeer.mp3`: optional user replacement for the bundled recording.

## Useful Commands

```bash
docker logs -f adhan-manager
docker exec -it adhan-manager crontab -l
docker exec -it adhan-manager python /app/update_adhan.py
docker exec -it adhan-manager python /app/trigger_ha.py "$PUBLIC_BASE_URL/audio/adhan_final.mp3" 0.5
```
