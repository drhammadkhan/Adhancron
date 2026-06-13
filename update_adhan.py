from __future__ import annotations

import os

from apply_adhan_cron import apply_updates


def run() -> int:
    print(f"--- Adhan Update Start: {os.uname().nodename} ---")
    exit_code = apply_updates()
    print("--- Adhan Update Complete ---")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(run())
