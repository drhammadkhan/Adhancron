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
apt-get install -y docker.io nginx avahi-daemon curl ca-certificates x11-xserver-utils

if apt-cache show chromium >/dev/null 2>&1; then
  apt-get install -y chromium
else
  apt-get install -y chromium-browser
fi
if apt-cache show unclutter-xfixes >/dev/null 2>&1; then
  apt-get install -y unclutter-xfixes
else
  apt-get install -y unclutter
fi
if apt-cache show docker-compose-plugin >/dev/null 2>&1; then
  apt-get install -y docker-compose-plugin
else
  apt-get install -y docker-compose
fi

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

install -d -o "$DESKTOP_USER" -g "$DESKTOP_USER" "$DESKTOP_HOME/.local/bin" "$DESKTOP_HOME/.config/autostart"
install -m 0755 -o "$DESKTOP_USER" -g "$DESKTOP_USER" "$SOURCE_DIR/adhancron-kiosk.sh" "$DESKTOP_HOME/.local/bin/adhancron-kiosk"
cat > "$DESKTOP_HOME/.config/autostart/adhancron-kiosk.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Adhan Clock
Comment=Open the full-screen Adhan Clock display
Exec=$DESKTOP_HOME/.local/bin/adhancron-kiosk
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
chown "$DESKTOP_USER:$DESKTOP_USER" "$DESKTOP_HOME/.config/autostart/adhancron-kiosk.desktop"
chmod 0644 "$DESKTOP_HOME/.config/autostart/adhancron-kiosk.desktop"

usermod -aG docker,audio,video,render "$DESKTOP_USER" 2>/dev/null || usermod -aG docker,audio,video "$DESKTOP_USER"
systemctl daemon-reload
systemctl enable --now docker avahi-daemon nginx adhancron-update.timer
/opt/adhancron/update.sh

echo
echo "Adhan Clock is installed."
echo "Dashboard: http://adhanclock.local"
echo "Clock view: http://adhanclock.local/display"
echo "Reboot once to start the full-screen display: sudo reboot"
