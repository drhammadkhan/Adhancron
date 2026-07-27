#!/usr/bin/env bash
set -euo pipefail

cd /opt/adhancron
if docker compose version >/dev/null 2>&1; then
  COMPOSE=(docker compose)
else
  COMPOSE=(docker-compose)
fi

# Older Adhancron installs may have created the fixed container name under a
# different Compose project. Replacing the container does not delete its image
# or any bind-mounted data, and allows this installation to take ownership.
EXISTING_CONTAINER="$(docker container ls -aq \
  --filter 'name=^/adhan-manager$' | head -n 1)"
if [[ -n "$EXISTING_CONTAINER" ]]; then
  EXISTING_PROJECT="$(docker inspect --format \
    '{{ index .Config.Labels "com.docker.compose.project" }}' \
    "$EXISTING_CONTAINER" 2>/dev/null || true)"
  if [[ "$EXISTING_PROJECT" != "adhanclock" ]]; then
    echo "Replacing an existing Adhancron container from an older installation..."
    docker rm -f "$EXISTING_CONTAINER" >/dev/null
  fi
fi

"${COMPOSE[@]}" --env-file adhancron.env pull
"${COMPOSE[@]}" --env-file adhancron.env up -d --remove-orphans
docker image prune -f >/dev/null
