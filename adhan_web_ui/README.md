# Adhan Web UI

FastAPI app for managing adhan cron jobs.

## Features

- Lists crontab entries that call `trigger_ha.py`.
- Adds prayer labels where missing.
- Preserves non-adhan cron entries.
- Updates job time and enabled/disabled state.
- Stores manual override flags in `/data/adhan_overrides.json` by default.
- Exposes `/audio/{filename}` for MP3 playback with byte-range support.
- Stores both observed Eid dates and a configurable repeated-takbeer window.
- Exposes the bundled or user-replaced takbeer at `/audio/eid_takbeer.mp3`.
- Provides separate adhan/takbeer test actions and Eid status/countdown data.

The Docker entrypoint starts this app as `adhan_web_ui.app:app` on port `8090`.
