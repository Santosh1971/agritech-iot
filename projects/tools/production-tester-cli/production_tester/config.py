"""
Bench-station settings, persisted locally -- mirrors the Android app's
BenchConfig.kt. testApSsid/testApPassword are the office WiFi the DUT
(and, once it gets there, the jig) join for the WiFi+MQTT phase -- see
docs/testing/PRODUCTION_TOOL_SPEC_V2.md §4.1 for why this is the office
router, never a phone hotspot.
"""
from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from pathlib import Path

CONFIG_FILE = Path.home() / ".nbagri_production_tester" / "config.json"


@dataclass
class BenchConfig:
    # Default to this bench's actual office WiFi -- set explicitly
    # 2026-09-18, no more blank-default risk (the Android app's earlier
    # "blank default caused a wasted test run" lesson). Still editable via
    # `python run.py --settings`.
    office_wifi_ssid: str = "Airtel_Santosh"
    office_wifi_password: str = "8197239206"
    expected_calibration_ppl: int = 450
    flow_test_pulse_count: int = 450
    operator_name: str = ""
    station_name: str = "bench-1"

    @staticmethod
    def load() -> "BenchConfig":
        if CONFIG_FILE.exists():
            try:
                return BenchConfig(**json.loads(CONFIG_FILE.read_text()))
            except (json.JSONDecodeError, OSError, TypeError):
                pass
        return BenchConfig()

    def save(self) -> None:
        CONFIG_FILE.parent.mkdir(parents=True, exist_ok=True)
        CONFIG_FILE.write_text(json.dumps(asdict(self), indent=2))
