from __future__ import annotations

import json
import threading
from pathlib import Path

class OverrideManager:
    def __init__(self, config_path: str | Path) -> None:
        self.path = Path(config_path)
        self._lock = threading.Lock()
        if not self.path.exists():
            self._save({})

    def _save(self, data: dict) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)

    def get_overrides(self) -> dict[str, bool]:
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                return json.load(f)
        except (json.JSONDecodeError, FileNotFoundError):
            return {}

    def set_override(self, prayer_id: str, manual: bool) -> None:
        with self._lock:
            data = self.get_overrides()
            data[prayer_id] = manual
            self._save(data)

    def is_manual(self, prayer_id: str) -> bool:
        overrides = self.get_overrides()
        return overrides.get(prayer_id, False)
