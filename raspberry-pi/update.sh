#!/usr/bin/env bash
set -euo pipefail

cd /opt/adhancron
if docker compose version >/dev/null 2>&1; then
  COMPOSE=(docker compose)
else
  COMPOSE=(docker-compose)
fi

"${COMPOSE[@]}" --env-file adhancron.env pull
"${COMPOSE[@]}" --env-file adhancron.env up -d --remove-orphans
docker image prune -f >/dev/null
