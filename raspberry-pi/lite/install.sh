#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
  echo "Run this installer with sudo: sudo ./raspberry-pi/lite/install.sh" >&2
  exit 1
fi

ARCH="$(uname -m)"
if [[ "$ARCH" != "aarch64" && "$ARCH" != "arm64" ]]; then
  echo "Adhan Clock Lite requires 64-bit Raspberry Pi OS (detected: $ARCH)." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BASE_DIR="/opt/adhancron"
INITIAL_COMMIT="$(git -C "$REPO_DIR" rev-parse HEAD 2>/dev/null || date -u +%Y%m%d%H%M%S)"
INITIAL_RELEASE="$BASE_DIR/releases/$INITIAL_COMMIT"
INITIAL_VENV="$BASE_DIR/venvs/$INITIAL_COMMIT"
REPOSITORY="$(git -C "$REPO_DIR" remote get-url origin 2>/dev/null || echo https://github.com/drhammadkhan/Adhancron.git)"

echo "Installing the native Adhan Clock Lite appliance..."
apt-get update
apt-get install -y --no-install-recommends \
  avahi-daemon build-essential ca-certificates cage cog cron curl git \
  libffi-dev libssl-dev mpg123 nginx python3 python3-dev python3-venv seatd

if ! id adhanclock >/dev/null 2>&1; then
  useradd --system --create-home --home-dir /var/lib/adhanclock \
    --shell /usr/sbin/nologin adhanclock
fi
for group in audio video render input seat; do
  if getent group "$group" >/dev/null; then
    usermod -aG "$group" adhanclock
  fi
done

install -d -m 0755 \
  "$BASE_DIR/bin" "$BASE_DIR/data" "$BASE_DIR/releases" "$BASE_DIR/venvs" \
  /etc/adhancron

if [[ ! -d "$BASE_DIR/source/.git" ]]; then
  git clone --filter=blob:none "$REPOSITORY" "$BASE_DIR/source"
fi

if [[ ! -d "$INITIAL_RELEASE" ]]; then
  install -d -m 0755 "$INITIAL_RELEASE"
  git -C "$REPO_DIR" archive HEAD | tar -x -C "$INITIAL_RELEASE"
fi

if [[ ! -x "$INITIAL_VENV/bin/python" ]]; then
  python3 -m venv "$INITIAL_VENV"
  "$INITIAL_VENV/bin/pip" install --upgrade pip wheel
  "$INITIAL_VENV/bin/pip" install -r "$INITIAL_RELEASE/requirements.txt"
fi

ln -sfn "$INITIAL_RELEASE" "$BASE_DIR/current"
ln -sfn "$INITIAL_VENV" "$BASE_DIR/venv"

install -m 0755 "$SCRIPT_DIR/native-start.sh" "$BASE_DIR/bin/native-start"
install -m 0755 "$SCRIPT_DIR/kiosk.sh" "$BASE_DIR/bin/kiosk"
install -m 0755 "$SCRIPT_DIR/update-native.sh" "$BASE_DIR/bin/update-native"

if [[ ! -f /etc/adhancron/adhancron.env ]]; then
  install -m 0600 "$SCRIPT_DIR/adhancron.env.example" /etc/adhancron/adhancron.env
  SYSTEM_TIMEZONE="$(cat /etc/timezone 2>/dev/null || echo Europe/London)"
  sed -i "s|^TZ=.*|TZ=$SYSTEM_TIMEZONE|" /etc/adhancron/adhancron.env
fi

hostnamectl set-hostname adhanclock
if grep -qE '^127\.0\.1\.1\s+' /etc/hosts; then
  sed -i -E 's/^127\.0\.1\.1\s+.*/127.0.1.1\tadhanclock/' /etc/hosts
else
  printf '127.0.1.1\tadhanclock\n' >> /etc/hosts
fi

rm -f /etc/nginx/sites-enabled/default
install -m 0644 "$REPO_DIR/raspberry-pi/nginx-adhanclock.conf" \
  /etc/nginx/sites-available/adhanclock
ln -sfn /etc/nginx/sites-available/adhanclock /etc/nginx/sites-enabled/adhanclock
nginx -t

install -m 0644 "$SCRIPT_DIR/adhancron.service" /etc/systemd/system/adhancron.service
install -m 0644 "$SCRIPT_DIR/adhanclock-display.service" \
  /etc/systemd/system/adhanclock-display.service
install -m 0644 "$SCRIPT_DIR/adhancron-native-update.service" \
  /etc/systemd/system/adhancron-native-update.service
install -m 0644 "$SCRIPT_DIR/adhancron-native-update.timer" \
  /etc/systemd/system/adhancron-native-update.timer

systemctl daemon-reload
systemctl disable --now getty@tty1.service 2>/dev/null || true
systemctl enable --now \
  avahi-daemon cron nginx seatd adhancron.service \
  adhanclock-display.service adhancron-native-update.timer

echo
echo "Adhan Clock Lite is installed."
echo "Dashboard: http://adhanclock.local"
echo "Clock view: http://adhanclock.local/display"
echo "Reboot once to give the display user its new device permissions: sudo reboot"
