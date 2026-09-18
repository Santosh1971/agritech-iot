"""
Test report model and local history store -- mirrors the Android app's
TestReport.kt/ReportStore, same step order and JSON shape, so historical
data from either tool lands in a compatible format.
"""
from __future__ import annotations

import csv
import json
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

# Fixed step order -- see docs/testing/PRODUCTION_TOOL_SPEC_V2.md §5 and
# JIG_NETWORK_BRIDGE_SPEC.md. The "_mqtt" steps re-verify relay/flow/RTC
# once the DUT has left the SoftAP for the office WiFi -- commanded over
# MQTT through the jig, not the laptop (which never touches WiFi at all).
PRODUCTION_TEST_STEPS = [
    "flash", "boot_log", "factory_reset", "calibrate",
    "relay_test", "flow_sensor", "rtc", "wifi_mqtt",
    "relay_test_mqtt", "flow_sensor_mqtt", "rtc_mqtt",
    "ship_clean_reset", "factory_reset_confirmed",
]

STEP_LABELS = {
    "flash": "Flash firmware",
    "boot_log": "Boot log check",
    "factory_reset": "Factory reset",
    "calibrate": "Set calibration",
    "relay_test": "Relay physical test",
    "flow_sensor": "Flow sensor accuracy",
    "rtc": "RTC sync (SoftAP)",
    "wifi_mqtt": "WiFi + MQTT connect",
    "relay_test_mqtt": "Relay test (via MQTT)",
    "flow_sensor_mqtt": "Flow sensor (via MQTT)",
    "rtc_mqtt": "RTC check (via MQTT)",
    "ship_clean_reset": "Ship-clean reset (via MQTT)",
    "factory_reset_confirmed": "Factory reset confirmed (SoftAP)",
}

REPORT_DIR = Path.home() / ".nbagri_production_tester" / "reports"


@dataclass
class StepResult:
    name: str
    passed: Optional[bool] = None
    detail: str = ""

    def to_json(self) -> dict:
        return {"passed": self.passed, "detail": self.detail}


@dataclass
class TestReport:
    device_id: Optional[str]
    firmware_version: Optional[str]
    build_label: str
    operator: str
    station: str
    steps: dict = field(default_factory=dict)
    timestamp_utc: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    @property
    def overall_passed(self) -> bool:
        return bool(self.steps) and all(s.passed is True for s in self.steps.values())

    def to_json(self) -> dict:
        return {
            "device_id": self.device_id,
            "firmware_version": self.firmware_version,
            "build_label": self.build_label,
            "timestamp_utc": self.timestamp_utc.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "operator": self.operator,
            "station": self.station,
            "steps": {name: s.to_json() for name, s in self.steps.items()},
            "overall_passed": self.overall_passed,
        }


def save(report: TestReport) -> Path:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    filename = f"{report.timestamp_utc.strftime('%Y%m%dT%H%M%SZ')}_{report.device_id or 'UNKNOWN'}.json"
    path = REPORT_DIR / filename
    path.write_text(json.dumps(report.to_json(), indent=2))
    return path


def export_csv() -> Path:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    rows = []
    for f in sorted(REPORT_DIR.glob("*.json")):
        try:
            rows.append(json.loads(f.read_text()))
        except (json.JSONDecodeError, OSError):
            continue
    csv_path = REPORT_DIR / f"fg1_test_results_{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}.csv"
    with csv_path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["timestamp_utc", "device_id", "tier", "passed", "steps_json"])
        for r in rows:
            steps_compact = json.dumps({k: v.get("passed") is True for k, v in r.get("steps", {}).items()})
            writer.writerow([
                r.get("timestamp_utc", ""), r.get("device_id", "UNKNOWN"),
                "production", r.get("overall_passed", False), steps_compact,
            ])
    return csv_path
