#!/usr/bin/env bash
set -euo pipefail

APP_DIR="${ADHAN_APP_DIR:-/opt/adhancron/current}"
DATA_DIR="${ADHAN_DATA_DIR:-/opt/adhancron/data}"
PYTHON_EXECUTABLE="${ADHAN_PYTHON:-/opt/adhancron/venv/bin/python}"
CONFIG_FILE="${ADHAN_CONFIG_FILE:-/etc/adhancron/adhancron.env}"
ENV_FILE="$DATA_DIR/adhan.env"

if [[ -f "$CONFIG_FILE" ]]; then
  set -a
  # shellcheck disable=SC1090
  . "$CONFIG_FILE"
  set +a
fi

mkdir -p "$DATA_DIR"
touch "$DATA_DIR/adhan.log" "$DATA_DIR/adhan_update.log" "$DATA_DIR/eid_takbeer.log"

write_runtime_env() {
  umask 077
  {
    printf 'export ADHAN_APP_DIR=%q\n' "$APP_DIR"
    printf 'export ADHAN_DATA_DIR=%q\n' "$DATA_DIR"
    printf 'export ADHAN_PYTHON=%q\n' "$PYTHON_EXECUTABLE"
    while IFS= read -r name; do
      [[ "$name" =~ ^[A-Z][A-Z0-9_]*$ ]] || continue
      if [[ -v "$name" ]]; then
        printf 'export %s=%q\n' "$name" "${!name}"
      fi
    done < <(
      printf '%s\n' \
        HA_TOKEN HA_URL HA_ENTITY_ID ADHAN_PLAYBACK_METHOD \
        ADHAN_ATTACHED_AUDIO_ENABLED ADHAN_ALSA_DEVICE \
        GOOGLE_CAST_HOST GOOGLE_CAST_PORT AIRPLAY_IDENTIFIER DLNA_LOCATION \
        ADHAN_AIRPLAY_TIMEOUT ADHAN_DLNA_TIMEOUT HA_REQUEST_TIMEOUT \
        ADHAN_USE_ANNOUNCE ADHAN_PLAY_ATTEMPTS ADHAN_PLAYBACK_TIMEOUT \
        ADHAN_CAST_CONNECT_TIMEOUT ADHAN_PLAYBACK_POLL_INTERVAL \
        ADHAN_MEDIA_CONTENT_TYPE PUBLIC_BASE_URL ADHAN_AUDIO_BASE_URL \
        ADHAN_AUDIO_URL ADHAN_AUDIO_FILE ADHAN_TAKBEER_URL \
        ADHAN_TAKBEER_FILE ADHAN_VOLUME PRAYER_TIMES_FILE \
        ADHAN_LATITUDE ADHAN_LONGITUDE ADHAN_OVERRIDE_FILE \
        ADHAN_STATUS_FILE ADHAN_SETTINGS_FILE TZ
    )
  } > "$ENV_FILE"
  chmod 0600 "$ENV_FILE"
}

install_manager_cron() {
  local current update_line eid_line
  current="$(crontab -l 2>/dev/null || true)"
  current="$(printf '%s\n' "$current" | sed \
    -e '/# \[Adhan Manager Update\]/{N;d;}' \
    -e '/# \[Adhan Eid Scheduler\]/{N;d;}')"
  update_line="5 0 * * * cd $APP_DIR && . $ENV_FILE && $PYTHON_EXECUTABLE $APP_DIR/update_adhan.py >> $DATA_DIR/adhan_update.log 2>&1"
  eid_line="* * * * * cd $APP_DIR && . $ENV_FILE && $PYTHON_EXECUTABLE $APP_DIR/eid_schedule.py >> $DATA_DIR/eid_takbeer.log 2>&1"
  {
    printf '%s\n' "$current"
    printf '# [Adhan Manager Update]\n%s\n' "$update_line"
    printf '# [Adhan Eid Scheduler]\n%s\n' "$eid_line"
  } | sed '/^$/N;/^\n$/D' | crontab -
}

write_runtime_env
install_manager_cron

cd "$APP_DIR"
# A missing location must not prevent the dashboard from starting.
"$PYTHON_EXECUTABLE" "$APP_DIR/update_adhan.py" >> "$DATA_DIR/adhan_update.log" 2>&1 || true

exec "$PYTHON_EXECUTABLE" -m uvicorn adhan_web_ui.app:app \
  --host 127.0.0.1 \
  --port 8090
