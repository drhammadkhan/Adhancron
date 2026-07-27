#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
  echo "Run this installer with sudo: sudo ./raspberry-pi/install.sh" >&2
  exit 1
fi

ARCH="$(uname -m)"
if [[ "$ARCH" != "aarch64" && "$ARCH" != "arm64" ]]; then
  echo "Adhan Clock requires 64-bit Raspberry Pi OS (detected: $ARCH)." >&2
  exit 1
fi

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DESKTOP_USER="${SUDO_USER:-${ADHAN_DESKTOP_USER:-}}"
if [[ -z "$DESKTOP_USER" || "$DESKTOP_USER" == "root" ]]; then
  DESKTOP_USER="$(getent passwd 1000 | cut -d: -f1 || true)"
fi
if [[ -z "$DESKTOP_USER" ]]; then
  echo "Unable to determine the Raspberry Pi desktop user." >&2
  exit 1
fi
DESKTOP_HOME="$(getent passwd "$DESKTOP_USER" | cut -d: -f6)"

echo "Installing Adhan Clock for $DESKTOP_USER..."
apt-get update
apt-get install -y docker.io nginx avahi-daemon curl ca-certificates \
  cage cog seatd
if apt-cache show docker-compose-plugin >/dev/null 2>&1; then
  apt-get install -y docker-compose-plugin
else
  apt-get install -y docker-compose
fi

if ! id adhanclock >/dev/null 2>&1; then
  useradd --system --create-home --home-dir /var/lib/adhanclock \
    --shell /usr/sbin/nologin adhanclock
fi
for group in audio video render input seat; do
  if getent group "$group" >/dev/null; then
    usermod -aG "$group" adhanclock
  fi
done

install -d -m 0755 /opt/adhancron /opt/adhancron/data
install -m 0644 "$SOURCE_DIR/docker-compose.yml" /opt/adhancron/docker-compose.yml
if [[ ! -f /opt/adhancron/adhancron.env ]]; then
  install -m 0600 "$SOURCE_DIR/adhancron.env.example" /opt/adhancron/adhancron.env
  SYSTEM_TIMEZONE="$(cat /etc/timezone 2>/dev/null || echo Europe/London)"
  sed -i "s|^TZ=.*|TZ=$SYSTEM_TIMEZONE|" /opt/adhancron/adhancron.env
fi
install -m 0755 "$SOURCE_DIR/update.sh" /opt/adhancron/update.sh

hostnamectl set-hostname adhanclock
if grep -qE '^127\.0\.1\.1\s+' /etc/hosts; then
  sed -i -E 's/^127\.0\.1\.1\s+.*/127.0.1.1\tadhanclock/' /etc/hosts
else
  printf '127.0.1.1\tadhanclock\n' >> /etc/hosts
fi

rm -f /etc/nginx/sites-enabled/default
install -m 0644 "$SOURCE_DIR/nginx-adhanclock.conf" /etc/nginx/sites-available/adhanclock
ln -sfn /etc/nginx/sites-available/adhanclock /etc/nginx/sites-enabled/adhanclock
nginx -t

install -m 0644 "$SOURCE_DIR/adhancron-update.service" /etc/systemd/system/adhancron-update.service
install -m 0644 "$SOURCE_DIR/adhancron-update.timer" /etc/systemd/system/adhancron-update.timer
install -m 0755 "$SOURCE_DIR/adhancron-display.sh" /opt/adhancron/display
install -m 0644 "$SOURCE_DIR/adhanclock-display.service" /etc/systemd/system/adhanclock-display.service

usermod -aG docker,audio,video,render "$DESKTOP_USER" 2>/dev/null || usermod -aG docker,audio,video "$DESKTOP_USER"
systemctl daemon-reload
systemctl disable --now getty@tty1.service 2>/dev/null || true
systemctl enable --now docker avahi-daemon nginx seatd adhancron-update.timer
/opt/adhancron/update.sh
systemctl enable --now adhanclock-display.service

echo
echo "Adhan Clock is installed."
echo "Dashboard: http://adhanclock.local"
echo "Clock view: http://adhanclock.local/display"
echo "The HDMI display service starts automatically. Reboot if the screen does not switch within a minute: sudo reboot"
