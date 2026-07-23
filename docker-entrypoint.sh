#!/usr/bin/env bash
set -euo pipefail

APP_DIR="${ADHAN_APP_DIR:-/app}"
DATA_DIR="${ADHAN_DATA_DIR:-/data}"
ENV_FILE="$DATA_DIR/adhan.env"
PYTHON_EXECUTABLE="${ADHAN_PYTHON:-/usr/local/bin/python}"

mkdir -p "$DATA_DIR"
touch "$DATA_DIR/adhan.log" "$DATA_DIR/adhan_update.log" "$DATA_DIR/eid_takbeer.log"

write_env_file() {
  umask 077
  {
    printf 'export ADHAN_APP_DIR=%q\n' "$APP_DIR"
    printf 'export ADHAN_DATA_DIR=%q\n' "$DATA_DIR"
    printf 'export ADHAN_PYTHON=%q\n' "$PYTHON_EXECUTABLE"
    printf 'export HA_TOKEN=%q\n' "${HA_TOKEN:-}"
    printf 'export HA_URL=%q\n' "${HA_URL:-http://homeassistant.local:8123}"
    printf 'export HA_ENTITY_ID=%q\n' "${HA_ENTITY_ID:-media_player.bedroom_speaker}"
    printf 'export ADHAN_PLAYBACK_METHOD=%q\n' "${ADHAN_PLAYBACK_METHOD:-home_assistant}"
    printf 'export ADHAN_ALSA_DEVICE=%q\n' "${ADHAN_ALSA_DEVICE:-default}"
    printf 'export GOOGLE_CAST_HOST=%q\n' "${GOOGLE_CAST_HOST:-}"
    printf 'export GOOGLE_CAST_PORT=%q\n' "${GOOGLE_CAST_PORT:-8009}"
    printf 'export AIRPLAY_IDENTIFIER=%q\n' "${AIRPLAY_IDENTIFIER:-}"
    printf 'export DLNA_LOCATION=%q\n' "${DLNA_LOCATION:-}"
    printf 'export ADHAN_AIRPLAY_TIMEOUT=%q\n' "${ADHAN_AIRPLAY_TIMEOUT:-5}"
    printf 'export ADHAN_DLNA_TIMEOUT=%q\n' "${ADHAN_DLNA_TIMEOUT:-5}"
    printf 'export HA_REQUEST_TIMEOUT=%q\n' "${HA_REQUEST_TIMEOUT:-15}"
    printf 'export ADHAN_USE_ANNOUNCE=%q\n' "${ADHAN_USE_ANNOUNCE:-true}"
    printf 'export ADHAN_PLAY_ATTEMPTS=%q\n' "${ADHAN_PLAY_ATTEMPTS:-2}"
    printf 'export ADHAN_PLAYBACK_TIMEOUT=%q\n' "${ADHAN_PLAYBACK_TIMEOUT:-15}"
    printf 'export ADHAN_PLAYBACK_POLL_INTERVAL=%q\n' "${ADHAN_PLAYBACK_POLL_INTERVAL:-1}"
    printf 'export ADHAN_MEDIA_CONTENT_TYPE=%q\n' "${ADHAN_MEDIA_CONTENT_TYPE:-audio/mpeg}"
    printf 'export PUBLIC_BASE_URL=%q\n' "${PUBLIC_BASE_URL:-}"
    printf 'export ADHAN_AUDIO_BASE_URL=%q\n' "${ADHAN_AUDIO_BASE_URL:-}"
    printf 'export ADHAN_AUDIO_URL=%q\n' "${ADHAN_AUDIO_URL:-}"
    printf 'export ADHAN_AUDIO_FILE=%q\n' "${ADHAN_AUDIO_FILE:-adhan_final.mp3}"
    printf 'export ADHAN_TAKBEER_URL=%q\n' "${ADHAN_TAKBEER_URL:-}"
    printf 'export ADHAN_TAKBEER_FILE=%q\n' "${ADHAN_TAKBEER_FILE:-eid_takbeer.mp3}"
    printf 'export ADHAN_VOLUME=%q\n' "${ADHAN_VOLUME:-0.8}"
    printf 'export PRAYER_TIMES_FILE=%q\n' "${PRAYER_TIMES_FILE:-$DATA_DIR/prayer_times.json}"
    printf 'export ADHAN_LATITUDE=%q\n' "${ADHAN_LATITUDE:-}"
    printf 'export ADHAN_LONGITUDE=%q\n' "${ADHAN_LONGITUDE:-}"
    printf 'export ADHAN_OVERRIDE_FILE=%q\n' "${ADHAN_OVERRIDE_FILE:-$DATA_DIR/adhan_overrides.json}"
    printf 'export ADHAN_STATUS_FILE=%q\n' "${ADHAN_STATUS_FILE:-$DATA_DIR/adhan_update_status.json}"
    printf 'export ADHAN_SETTINGS_FILE=%q\n' "${ADHAN_SETTINGS_FILE:-$DATA_DIR/adhan_settings.json}"
  } > "$ENV_FILE"
}

install_manager_cron() {
  local update_line="5 0 * * * cd $APP_DIR && . $ENV_FILE && $PYTHON_EXECUTABLE $APP_DIR/update_adhan.py >> $DATA_DIR/adhan_update.log 2>&1"
  local eid_line="* * * * * cd $APP_DIR && . $ENV_FILE && $PYTHON_EXECUTABLE $APP_DIR/eid_schedule.py >> $DATA_DIR/eid_takbeer.log 2>&1"
  local current
  current="$(crontab -l 2>/dev/null || true)"

  if ! grep -qF '# [Adhan Manager Update]' <<< "$current"; then
    {
      printf '%s\n' "$current"
      printf '# [Adhan Manager Update]\n'
      printf '%s\n' "$update_line"
    } | sed '/^$/N;/^\n$/D' | crontab -
    current="$(crontab -l 2>/dev/null || true)"
  fi

  if ! grep -qF '# [Adhan Eid Scheduler]' <<< "$current"; then
    {
      printf '%s\n' "$current"
      printf '# [Adhan Eid Scheduler]\n'
      printf '%s\n' "$eid_line"
    } | sed '/^$/N;/^\n$/D' | crontab -
  fi
}

echo "--- Starting Adhan Manager Container ---"
write_env_file

echo "Step 1: Installing updater cron and applying today's schedule..."
install_manager_cron
cd "$APP_DIR"
. "$ENV_FILE"
"$PYTHON_EXECUTABLE" "$APP_DIR/update_adhan.py"

echo "Step 2: Starting cron daemon..."
cron

echo "Step 3: Starting Web UI on port 8090..."
exec "$PYTHON_EXECUTABLE" -m uvicorn adhan_web_ui.app:app \
  --host 0.0.0.0 \
  --port 8090
