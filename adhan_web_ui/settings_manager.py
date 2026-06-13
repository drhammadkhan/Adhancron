from __future__ import annotations

import json
import os
import threading
from pathlib import Path


class SettingsManager:
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)
        self._lock = threading.Lock()

    def get_settings(self) -> dict[str, str]:
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (json.JSONDecodeError, FileNotFoundError):
            return {}

        return {
            key: value
            for key, value in data.items()
            if key in {"ha_token", "ha_url", "ha_entity_id"} and isinstance(value, str)
        }

    def update_settings(self, updates: dict[str, str | None]) -> dict[str, str]:
        with self._lock:
            data = self.get_settings()
            for key, value in updates.items():
                if key not in {"ha_token", "ha_url", "ha_entity_id"}:
                    continue
                if value is None:
                    continue
                clean_value = value.strip()
                if clean_value:
                    data[key] = clean_value

            self.path.parent.mkdir(parents=True, exist_ok=True)
            with open(self.path, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
            os.chmod(self.path, 0o600)
            return data
