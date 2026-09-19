"""
USB-serial client for the test jig (ESP32) -- see
products/FG1-flowguard/testing/jig_firmware/esp32_usb_serial/src/main.cpp
for the firmware this talks to, and
products/FG1-flowguard/docs/testing/JIG_NETWORK_BRIDGE_SPEC.md for the
full protocol. Ported from the Android app's JigSerialClient.kt +
JigNetworkBridge.kt.

This is the ONLY thing in the whole system that ever touches WiFi -- the
laptop just talks plain line-based USB-serial to this class, the jig does
the rest (joins the DUT's SoftAP, relays HTTP commands to it, later joins
the office WiFi and bridges MQTT too). No wireless-adb-style flakiness to
fight here: pyserial's blocking readline over a real OS serial port is a
mature, well-trodden path, unlike the Android USB-serial library that
caused most of this project's earlier debugging sessions.
"""
from __future__ import annotations

import json
import time
from typing import Callable, Optional

import serial

# Must match Config.h's SOFTAP_PASSWORD and the jig firmware's own copy of
# it -- a single shared value across the fleet, not per-device. Update all
# three together if it ever changes.
SOFTAP_PASSWORD = "water1234"


class JigClient:
    def __init__(self, port: str, baud: int = 115200, on_line: Optional[Callable[[str], None]] = None):
        self._ser = serial.Serial(port, baud, timeout=0.1)
        # Optional tap on every command sent and reply received -- for a
        # UI to show a live jig console alongside the guided test. The
        # jig has no unsolicited/async serial output of its own worth
        # tailing the way the DUT does (it only ever speaks in direct
        # reply to a command), so this taps the request/reply pairs
        # already flowing through _send_and_read_line() instead of
        # needing a separate continuous-read thread.
        self._on_line = on_line
        self._drain_boot_noise()

    def _drain_boot_noise(self, quiet_s: float = 0.4, max_wait_s: float = 3.0) -> None:
        """Reads and discards anything arriving until the port's been quiet
        for [quiet_s] seconds, capped at [max_wait_s] total. A flat short
        sleep wasn't enough margin right after a fresh flash+reboot
        specifically (as opposed to opening a port to an already-running
        jig) -- 2026-09-18 bench finding: PING sent too early got a
        delayed PONG that then showed up as the reply to the *next*
        command instead, a serial protocol desync. Adaptive draining
        handles both cases: near-instant when the jig's already settled,
        patient when it just rebooted and is still printing boot text."""
        deadline = time.monotonic() + max_wait_s
        last_byte_at = time.monotonic()
        while time.monotonic() < deadline:
            chunk = self._ser.read(256)
            if chunk:
                last_byte_at = time.monotonic()
            elif time.monotonic() - last_byte_at >= quiet_s:
                break
        self._ser.reset_input_buffer()

    def close(self) -> None:
        try:
            self._ser.close()
        except serial.SerialException:
            pass

    def _send_and_read_line(self, command: str, timeout_s: float) -> Optional[str]:
        if self._on_line:
            self._on_line(f">> {command}")
        self._ser.reset_input_buffer()
        self._ser.write((command + "\n").encode("utf-8"))
        deadline = time.monotonic() + timeout_s
        buf = bytearray()
        while time.monotonic() < deadline:
            chunk = self._ser.read(256)
            if not chunk:
                continue
            for b in chunk:
                if b == 0x0A:  # \n
                    reply = bytes(buf).decode("utf-8", errors="replace").strip()
                    if self._on_line:
                        self._on_line(f"<< {reply}")
                    return reply
                if b != 0x0D:  # skip \r
                    buf.append(b)
        if self._on_line:
            self._on_line("<< (no reply)")
        return None

    # ---------- base protocol ----------

    def ping(self, timeout_s: float = 2.0) -> bool:
        return self._send_and_read_line("PING", timeout_s) == "PONG"

    def relay_state(self, timeout_s: float = 2.0) -> Optional[bool]:
        reply = self._send_and_read_line("RELAY?", timeout_s)
        if reply == "RELAY:ON":
            return True
        if reply == "RELAY:OFF":
            return False
        return None

    def relay_state_confirmed(self, expect_on: bool, poll_s: float = 2.5) -> Optional[bool]:
        """relay_state(), but polls for [poll_s] instead of reading once.

        Bench-confirmed 2026-09-18: relay_test landed and dispatched on
        the DUT (visually confirmed clicking) and the SoftAP-phase relay
        check right next to this one in the code has never once missed
        it -- yet relay_test_mqtt's single RELAY? read after a flat 0.8s
        sleep reported OFF. The one thing different about this specific
        check is that it runs while the jig's own WiFi radio is actively
        transmitting (MQTT phase) -- the same class of RF-interference-
        on-a-nearby-signal issue already diagnosed once this session for
        the DUT's UART line, now most likely hitting the jig's own
        RELAY-sense read instead. A poll, not a single read, is the same
        fix that's already worked for every other flaky check tonight."""
        deadline = time.monotonic() + poll_s
        last = None
        while time.monotonic() < deadline:
            last = self.relay_state()
            if last is expect_on:
                return last
            time.sleep(0.3)
        return last

    def pulse(self, count: int) -> bool:
        timeout_s = 3.0 + (count // 200)
        return self._send_and_read_line(f"PULSE:{count}", timeout_s) == f"OK:{count}"

    def stop_square(self, timeout_s: float = 2.0) -> bool:
        """Stops the SQUARE:<hz> continuous wave on the same pin PULSE:<n>
        uses -- bench-confirmed 2026-09-18: an active square wave injects
        extra edges into individually-spaced PULSE:1 calls (which give
        loop() idle time to keep toggling it between pulses; a fast
        PULSE:<n> burst blocks loop() entirely so it never had this
        problem). Defensive -- safe to call even if nothing is running."""
        return self._send_and_read_line("SQUARE:STOP", timeout_s) == "OK:SQUARE_STOPPED"

    # ---------- network bridge (JIG_NETWORK_BRIDGE_SPEC.md) ----------

    def join_ap(self, ssid: str, password: str, timeout_s: float = 12.0) -> Optional[str]:
        """Returns the raw reply ("JOINED:<ip>" or "JOIN_FAIL:<reason>")."""
        return self._send_and_read_line(f"JOIN_AP:{ssid}:{password}", timeout_s)

    def join_ap_with_poll(self, ssid: str, password: str, poll_s: float = 8.0) -> bool:
        """join_ap(), but treats a JOIN_FAIL/timeout reply as inconclusive
        rather than a real failure and polls wifi_status() for a bit
        instead of giving up immediately.

        The jig's JOIN_AP handler calls WiFi.begin() and gives up waiting
        after 10s, but never calls WiFi.disconnect() on that timeout -- so
        a JOIN_FAIL reply doesn't mean the association failed, only that
        it didn't finish within the jig's own window. Bench-confirmed
        2026-09-18: a real run reported "could not join" here (both for
        the DUT's SoftAP and separately for the office WiFi), but a plain
        WIFI_STATUS? check a few seconds later (no resend of JOIN_AP)
        showed it had connected anyway. Poll instead of immediately
        failing -- resending JOIN_AP would only restart the in-progress
        attempt."""
        reply = self.join_ap(ssid, password)
        if reply and reply.startswith("JOINED:"):
            return True
        deadline = time.monotonic() + poll_s
        while time.monotonic() < deadline:
            status = self.wifi_status(timeout_s=2.0)
            if status and status.startswith(f"WIFI:connected:{ssid}:"):
                return True
            time.sleep(1.0)
        return False

    def join_dut_softap(self, device_id: str) -> bool:
        return self.join_ap_with_poll(device_id, SOFTAP_PASSWORD)

    def leave_wifi(self, timeout_s: float = 3.0) -> bool:
        return self._send_and_read_line("LEAVE_WIFI", timeout_s) == "OK"

    def wifi_status(self, timeout_s: float = 2.0) -> Optional[str]:
        return self._send_and_read_line("WIFI_STATUS?", timeout_s)

    @staticmethod
    def _parse_http_reply(reply: Optional[str]) -> Optional[dict]:
        if not reply or not reply.startswith("HTTP_OK:"):
            return None
        try:
            data = json.loads(reply[len("HTTP_OK:"):])
        except json.JSONDecodeError:
            return None
        return data.get("data", data)

    def http_command(self, cmd: str, extra: Optional[dict] = None, timeout_s: float = 9.0) -> Optional[dict]:
        """Sends {"cmd": cmd, ...extra} to the DUT via the jig's HTTP client
        (while joined to the DUT's own SoftAP). Returns the "data" field of
        the response, or the whole response if there's no "data" key --
        same shape the DUT always replies with over WS or HTTP. None on
        any failure (jig unreachable, HTTP_FAIL, malformed JSON)."""
        payload = {"cmd": cmd, **(extra or {})}
        reply = self._send_and_read_line(f"HTTP_CMD:{json.dumps(payload)}", timeout_s)
        return self._parse_http_reply(reply)

    def lan_http_command(
        self, ip: str, cmd: str, extra: Optional[dict] = None, timeout_s: float = 9.0,
    ) -> Optional[dict]:
        """Same as http_command(), but targets an arbitrary IP on whatever
        network the jig is currently associated with, instead of the DUT's
        own SoftAP gateway (192.168.4.1) -- for once the DUT has left
        SoftAP for the office WiFi/hotspot the jig is also bridging MQTT
        over. 2026-09-18: added specifically to verify relay_test_mqtt/
        flow_sensor_mqtt/rtc_mqtt results this way instead of only through
        the jig's own MQTT status subscription or by parsing the DUT's
        serial output, both measurably less reliable under real network
        conditions than this same HTTP mechanism already proven via
        SoftAP all session."""
        payload = {"cmd": cmd, **(extra or {})}
        reply = self._send_and_read_line(f"LAN_HTTP_CMD:{ip}:{json.dumps(payload)}", timeout_s)
        return self._parse_http_reply(reply)

    def device_info(self, timeout_s: float = 9.0) -> Optional[dict]:
        return self.http_command("device_info", timeout_s=timeout_s)

    def lan_device_info(self, ip: str, timeout_s: float = 9.0) -> Optional[dict]:
        return self.lan_http_command(ip, "device_info", timeout_s=timeout_s)

    def mqtt_connect(self, device_id: str, timeout_s: float = 12.0) -> bool:
        return self._send_and_read_line(f"MQTT_CONNECT:{device_id}", timeout_s) == "MQTT_OK"

    def mqtt_command(self, cmd: str, extra: Optional[dict] = None, timeout_s: float = 5.0) -> bool:
        payload = {"cmd": cmd, **(extra or {})}
        return self._send_and_read_line(f"MQTT_CMD:{json.dumps(payload)}", timeout_s) == "MQTT_SENT"

    def mqtt_status(self, timeout_s: float = 2.0) -> Optional[dict]:
        """Latest retained status message received since mqtt_connect(), or
        None if nothing's arrived since the last call (each call drains it,
        same "hand back the freshest, then clear" contract as the Android
        app's MqttCommander.latestStatus())."""
        reply = self._send_and_read_line("MQTT_STATUS?", timeout_s)
        if not reply or not reply.startswith("STATUS:"):
            return None
        try:
            return json.loads(reply[len("STATUS:"):])
        except json.JSONDecodeError:
            return None

    def mqtt_command_with_retry(
        self, cmd: str, extra: Optional[dict] = None,
        attempts: int = 3, interval_s: float = 2.5,
        confirmed_by=None,
    ) -> Optional[dict]:
        """Publishes [cmd] up to [attempts] times, checking mqtt_status()
        after each. A single publish has a real chance of landing during
        one of the DUT's own WiFi/MQTT reconnect flaps and being silently
        dropped -- PubSubClient (both the DUT's and the jig's) has no
        persistent session/QoS>0. Returns the status that satisfied
        confirmed_by, or the last status seen if none did."""
        last = None
        for _ in range(attempts):
            self.mqtt_command(cmd, extra)
            status = self._poll_status(interval_s)
            if status is not None:
                last = status
                if confirmed_by is None or confirmed_by(status):
                    return status
        return last

    def _poll_status(self, timeout_s: float) -> Optional[dict]:
        deadline = time.monotonic() + timeout_s
        last = None
        while time.monotonic() < deadline:
            status = self.mqtt_status(timeout_s=max(0.2, deadline - time.monotonic()))
            if status is not None:
                last = status
            time.sleep(0.2)
        return last


def probe_for_jig(
    port: str, baud: int = 115200, on_line: Optional[Callable[[str], None]] = None,
) -> Optional[JigClient]:
    """Opens [port], sends PING, and returns a ready client if (and only
    if) it replies PONG -- otherwise closes the port and returns None.
    Used to identify which of the (possibly several) USB-serial devices
    on the machine is the jig, without assuming a fixed port order."""
    try:
        client = JigClient(port, baud, on_line=on_line)
    except serial.SerialException:
        return None
    if client.ping():
        return client
    client.close()
    return None
