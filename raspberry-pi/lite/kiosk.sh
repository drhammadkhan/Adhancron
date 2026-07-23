#!/usr/bin/env bash
set -euo pipefail

URL="${ADHAN_DISPLAY_URL:-http://127.0.0.1:8090/display}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/adhanclock}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

setterm --blank 0 --powersave off --powerdown 0 >/dev/null 2>&1 || true

until curl -fsS http://127.0.0.1:8090/api/jobs >/dev/null; do
  sleep 2
done

# Cog's DRM platform avoids an entire desktop environment. Cage is retained as
# a compatibility fallback for displays whose DRM driver requires Wayland.
if cog --platform=drm --webprocess-failure=restart "$URL"; then
  exit 0
fi

exec cage -s -- cog --platform=wl --webprocess-failure=restart "$URL"
