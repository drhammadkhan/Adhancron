# Adhan Manager

This project automates adhan playback through Home Assistant speakers.

The active packaging target is a Docker container that:

- Maintains cron jobs for Fajr, Dhuhr, Asr, Maghrib, and Isha.
- Updates the cron schedule daily from `prayer_times.json`.
- Calls Home Assistant to play `adhan_final.mp3` on the configured media player.
- Serves the MP3 from the web UI with byte-range support.
- Provides a web UI for editing prayer times, enabling/disabling jobs, manual overrides, and test playback.
- Lets you save the Home Assistant URL, speaker entity, and long-lived access token from the web UI.

The older ESP32/MAX7219 files are still present for display experiments, but they are not required for the Dockerized Home Assistant flow.

## Main Files

- `trigger_ha.py`: calls Home Assistant `media_player.volume_set` and `media_player.play_media`.
- `apply_adhan_cron.py`: seeds and updates cron jobs from `prayer_times.json`.
- `update_adhan.py`: small wrapper used by Docker startup and the daily cron updater.
- `adhan_web_ui/app.py`: FastAPI web UI and API.
- `adhan_web_ui/cron_manager.py`: crontab parser/editor for adhan jobs.
- `adhan_config.py`: shared container/runtime configuration.
- `adhan_final.mp3`: default audio played by Home Assistant.

## Docker Quick Start

```bash
docker build -t adhan-manager .
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

Open `http://localhost:8090` for the web UI.

Or use Compose:

```bash
docker compose up -d --build
```

See `DEPLOYMENT.md` for the full packaging notes.
