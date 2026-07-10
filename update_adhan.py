from __future__ import annotations

import os
from datetime import datetime

from apply_adhan_cron import apply_updates
from generate_prayer_times import ensure_configured_times


def run() -> int:
    print(f"--- Adhan Update Start: {os.uname().nodename} ---")
    try:
        result = ensure_configured_times(datetime.now())
        if result:
            print(result.summary)
        else:
            print("Prayer times already cover the current and following year")
    except ValueError as exc:
        print(f"Prayer-time generation skipped: {exc}")
    exit_code = apply_updates()
    print("--- Adhan Update Complete ---")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(run())
