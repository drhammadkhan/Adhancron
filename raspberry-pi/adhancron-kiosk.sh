#!/usr/bin/env bash
set -u

export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}"

for _ in $(seq 1 90); do
  if curl --fail --silent http://127.0.0.1:8090/api/version >/dev/null; then
    break
  fi
  sleep 2
done

xset s off >/dev/null 2>&1 || true
xset -dpms >/dev/null 2>&1 || true
xset s noblank >/dev/null 2>&1 || true

if command -v unclutter >/dev/null 2>&1; then
  unclutter --timeout 2 --jitter 5 >/dev/null 2>&1 &
elif command -v unclutter-xfixes >/dev/null 2>&1; then
  unclutter-xfixes --timeout 2 --jitter 5 >/dev/null 2>&1 &
fi

BROWSER="$(command -v chromium || command -v chromium-browser || true)"
if [[ -z "$BROWSER" ]]; then
  echo "Chromium is not installed" >&2
  exit 1
fi

exec "$BROWSER" \
  --kiosk \
  --noerrdialogs \
  --disable-infobars \
  --disable-session-crashed-bubble \
  --disable-features=Translate \
  --autoplay-policy=no-user-gesture-required \
  --check-for-update-interval=31536000 \
  http://adhanclock.local/display
