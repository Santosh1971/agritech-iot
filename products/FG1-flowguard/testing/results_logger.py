"""Logs one row per test run to a CSV -- device ID, timestamp,
overall pass/fail, and a JSON blob of per-step results, so you have a
real yield record and something to check if a customer reports an issue.

Real, runnable today.
"""
import csv
import json
from datetime import datetime, timezone
from pathlib import Path

LOG_PATH = Path(__file__).resolve().parent / "test_results.csv"

FIELDNAMES = ["timestamp_utc", "device_id", "tier", "passed", "steps_json"]


def log_result(device_id: str | None, tier: str, passed: bool, steps: dict):
    """steps: dict of step_name -> bool (or any JSON-serializable detail)."""
    is_new = not LOG_PATH.exists()
    with open(LOG_PATH, "a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDNAMES)
        if is_new:
            writer.writeheader()
        writer.writerow({
            "timestamp_utc": datetime.now(timezone.utc).isoformat(),
            "device_id": device_id or "UNKNOWN",
            "tier": tier,
            "passed": passed,
            "steps_json": json.dumps(steps),
        })


if __name__ == "__main__":
    log_result("SWC_001_TEST", "production", True, {"boot_log": True, "relay": True})
    print(f"Logged to {LOG_PATH}")
