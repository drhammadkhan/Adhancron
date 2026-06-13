#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="${ADHAN_WEB_VENV:-$ROOT_DIR/.venv-adhan-web-ui}"

cd "$ROOT_DIR"

if [ ! -d "$VENV_DIR" ]; then
  python3 -m venv "$VENV_DIR"
fi

if ! "$VENV_DIR/bin/python" -c "import fastapi, uvicorn" >/dev/null 2>&1; then
  "$VENV_DIR/bin/pip" install --upgrade pip
  "$VENV_DIR/bin/pip" install -r "$ROOT_DIR/adhan_web_ui/requirements.txt"
fi

export PYTHONPATH="$ROOT_DIR${PYTHONPATH:+:$PYTHONPATH}"
exec "$VENV_DIR/bin/python" -m uvicorn adhan_web_ui.app:app \
  --host 0.0.0.0 \
  --port "${PORT:-8090}"
