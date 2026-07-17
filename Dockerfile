FROM python:3.11-slim

LABEL org.opencontainers.image.title="Adhancron" \
      org.opencontainers.image.source="https://github.com/drhammadkhan/Adhancron" \
      org.opencontainers.image.version="2026.07.16.1"

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PYTHONPATH=/app \
    TZ=Europe/London \
    ADHAN_APP_DIR=/app \
    ADHAN_DATA_DIR=/data \
    HA_URL=http://homeassistant.local:8123 \
    HA_ENTITY_ID=media_player.bedroom_speaker \
    ADHAN_PLAYBACK_METHOD=home_assistant \
    GOOGLE_CAST_PORT=8009 \
    ADHAN_AIRPLAY_TIMEOUT=5 \
    ADHAN_DLNA_TIMEOUT=5 \
    ADHAN_AUDIO_FILE=adhan_final.mp3 \
    ADHAN_VOLUME=0.8 \
    ADHAN_USE_ANNOUNCE=true \
    ADHAN_PLAY_ATTEMPTS=2 \
    ADHAN_PLAYBACK_TIMEOUT=15 \
    ADHAN_CAST_CONNECT_TIMEOUT=30 \
    ADHAN_PLAYBACK_POLL_INTERVAL=1 \
    ADHAN_MEDIA_CONTENT_TYPE=audio/mpeg

WORKDIR /app

COPY requirements.txt .
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        cron \
        curl \
        g++ \
        tzdata \
    && pip install --no-cache-dir -r requirements.txt \
    && apt-get purge -y --auto-remove g++ \
    && rm -rf /var/lib/apt/lists/*

COPY . .
RUN chmod +x /app/docker-entrypoint.sh \
    && mkdir -p /data

VOLUME ["/data"]
EXPOSE 8090

HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8090/api/jobs >/dev/null || exit 1

ENTRYPOINT ["/app/docker-entrypoint.sh"]
