#!/usr/bin/env bash
set -euo pipefail

BASE_DIR="${ADHAN_INSTALL_DIR:-/opt/adhancron}"
SOURCE_DIR="$BASE_DIR/source"
RELEASES_DIR="$BASE_DIR/releases"
VENVS_DIR="$BASE_DIR/venvs"
REPOSITORY="${ADHAN_REPOSITORY:-https://github.com/drhammadkhan/Adhancron.git}"
BRANCH="${ADHAN_BRANCH:-main}"

mkdir -p "$RELEASES_DIR" "$VENVS_DIR"

if [[ ! -d "$SOURCE_DIR/.git" ]]; then
  git clone --filter=blob:none --branch "$BRANCH" "$REPOSITORY" "$SOURCE_DIR"
fi

git -C "$SOURCE_DIR" fetch --prune origin "$BRANCH"
COMMIT="$(git -C "$SOURCE_DIR" rev-parse "origin/$BRANCH")"
RELEASE_DIR="$RELEASES_DIR/$COMMIT"
VENV_DIR="$VENVS_DIR/$COMMIT"

if [[ ! -d "$RELEASE_DIR" ]]; then
  mkdir -p "$RELEASE_DIR"
  git -C "$SOURCE_DIR" archive "$COMMIT" | tar -x -C "$RELEASE_DIR"
fi

if [[ ! -x "$VENV_DIR/bin/python" ]]; then
  python3 -m venv "$VENV_DIR"
  "$VENV_DIR/bin/pip" install --upgrade pip wheel
  "$VENV_DIR/bin/pip" install -r "$RELEASE_DIR/requirements.txt"
fi

CURRENT_RELEASE="$(readlink -f "$BASE_DIR/current" 2>/dev/null || true)"
CURRENT_VENV="$(readlink -f "$BASE_DIR/venv" 2>/dev/null || true)"
if [[ "$CURRENT_RELEASE" == "$RELEASE_DIR" && "$CURRENT_VENV" == "$VENV_DIR" ]]; then
  exit 0
fi

switch_link() {
  local target="$1" link="$2" temporary="${link}.new"
  ln -sfn "$target" "$temporary"
  mv -Tf "$temporary" "$link"
}

switch_link "$RELEASE_DIR" "$BASE_DIR/current"
switch_link "$VENV_DIR" "$BASE_DIR/venv"

if systemctl restart adhancron.service &&
   curl --retry 12 --retry-delay 2 --retry-connrefused -fsS \
     http://127.0.0.1:8090/api/jobs >/dev/null; then
  systemctl try-restart adhanclock-display.service || true
  for path in "$RELEASES_DIR"/*; do
    [[ -d "$path" ]] || continue
    [[ "$path" == "$RELEASE_DIR" || "$path" == "$CURRENT_RELEASE" ]] || rm -rf "$path"
  done
  for path in "$VENVS_DIR"/*; do
    [[ -d "$path" ]] || continue
    [[ "$path" == "$VENV_DIR" || "$path" == "$CURRENT_VENV" ]] || rm -rf "$path"
  done
  exit 0
fi

echo "Update health check failed; restoring the previous release." >&2
if [[ -n "$CURRENT_RELEASE" && -n "$CURRENT_VENV" ]]; then
  switch_link "$CURRENT_RELEASE" "$BASE_DIR/current"
  switch_link "$CURRENT_VENV" "$BASE_DIR/venv"
  systemctl restart adhancron.service
fi
exit 1
