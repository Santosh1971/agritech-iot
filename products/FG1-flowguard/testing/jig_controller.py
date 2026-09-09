"""HTTP client for the test jig's controller (ESP8266, WiFi) -- see
docs/testing/PRODUCTION_TOOL_SPEC.md section 6.3 for the protocol, and
jig_firmware/esp8266_wifi/src/main.cpp for the firmware that implements
it.

Supersedes the original USB-serial version (still archived at
jig_firmware/serial_legacy/jig_controller.ino for reference) -- the jig
now joins the DUT's own SoftAP as a WiFi station (see spec section 6.2),
so this test PC needs to be on that same network to reach it, same as
DutClient.

Real, runnable once the jig is flashed and has joined a DUT's SoftAP.
Requires: pip install requests
"""
import time

import requests


class JigController:
    def __init__(self, host: str = "fg1jig.local", timeout_s: float = 5.0):
        self.host = host
        self.timeout_s = timeout_s

    def close(self):
        pass  # no persistent connection to tear down (plain HTTP)

    def _get(self, path: str, timeout_s: float | None = None, **params) -> dict | None:
        try:
            resp = requests.get(f"http://{self.host}{path}", params=params, timeout=timeout_s or self.timeout_s)
            return resp.json()
        except requests.RequestException:
            return None

    def _post(self, path: str, timeout_s: float | None = None, **params) -> dict | None:
        try:
            resp = requests.post(f"http://{self.host}{path}", params=params, timeout=timeout_s or self.timeout_s)
            return resp.json()
        except requests.RequestException:
            return None

    def ping(self) -> bool:
        result = self._get("/ping")
        return bool(result and result.get("ok"))

    def pulse(self, count: int) -> bool:
        """Emit exactly `count` pulses on the flow-sim output. Blocks
        (server-side) until the jig confirms it's done -- timeout scales
        with count since a large pulse train takes real time to emit.
        """
        result = self._post("/pulse", timeout_s=max(self.timeout_s, 5 + count / 200), n=count)
        return bool(result and result.get("ok") and result.get("emitted") == count)

    def relay_state(self) -> bool:
        """True if the jig currently senses the DUT's relay output as
        closed/energized.
        """
        result = self._get("/relay")
        return bool(result and result.get("state") == "on")

    def status(self) -> dict | None:
        """Debug info: which DUT SoftAP the jig thinks it's joined to."""
        return self._get("/status")


if __name__ == "__main__":
    import sys

    host_arg = sys.argv[1] if len(sys.argv) > 1 else "fg1jig.local"
    jig = JigController(host_arg)
    print("PING:", "OK" if jig.ping() else "FAIL")
    print("Status:", jig.status())
    print("Relay state:", "ON" if jig.relay_state() else "OFF")
