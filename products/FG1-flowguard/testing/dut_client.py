"""Sends {"cmd": ...} JSON commands to the DUT's Local WS API -- the
exact same protocol the mobile app uses in Local mode (see
mobile-app/flutter_app/lib/services/local_service.dart).

The test PC stays connected to the DUT's own SoftAP for the entire test
sequence. The DUT can run AP+STA simultaneously (see `ap_active` in
buildStatusJSON(), which checks WIFI_AP_STA), so device_info's
wifi_connected/mqtt_connected fields prove the STA-side test-AP join
worked without ever needing the test PC to switch networks mid-test.

Real, runnable today. Requires: pip install websocket-client
"""
import json
import time

import websocket

DEFAULT_URL = "ws://192.168.4.1/ws"


class DutClient:
    def __init__(self, url: str = DEFAULT_URL, timeout_s: float = 5.0):
        self.url = url
        self.timeout_s = timeout_s
        self.ws: websocket.WebSocket | None = None

    def connect(self):
        self.ws = websocket.create_connection(self.url, timeout=self.timeout_s)

    def close(self):
        if self.ws:
            self.ws.close()
            self.ws = None

    def send_command(self, cmd: str, **payload) -> dict:
        """Sends {"cmd": cmd, ...payload} and returns the parsed JSON
        response. Raises on timeout or malformed response.
        """
        if not self.ws:
            raise RuntimeError("Not connected -- call connect() first")
        msg = {"cmd": cmd, **payload}
        self.ws.send(json.dumps(msg))
        raw = self.ws.recv()
        return json.loads(raw)

    def device_info(self) -> dict:
        return self.send_command("device_info")

    def wait_for(self, predicate, timeout_s: float = 20.0, poll_interval_s: float = 1.0) -> dict | None:
        """Polls device_info() until predicate(info) is True or timeout.
        Returns the last info dict that satisfied the predicate, or None.
        """
        deadline = time.time() + timeout_s
        last_info = None
        while time.time() < deadline:
            last_info = self.device_info()
            if predicate(last_info):
                return last_info
            time.sleep(poll_interval_s)
        return None


if __name__ == "__main__":
    client = DutClient()
    client.connect()
    print(json.dumps(client.device_info(), indent=2))
    client.close()
