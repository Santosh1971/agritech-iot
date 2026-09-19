# Jig as Network Bridge — Specification v1

Status: Draft, not yet built. Proposes moving all WiFi/MQTT networking off
the phone and onto the test jig, over the existing USB-serial link. Builds
on [`PRODUCTION_TOOL_SPEC_V2.md`](PRODUCTION_TOOL_SPEC_V2.md), which remains
the current, running architecture until this is implemented and
bench-validated — this doc describes the next iteration, not a replacement
yet.

## 1. Problem this solves

Nearly every reliability issue hit while validating v2.7–2.10 traces back to
one thing: **the phone has a single WiFi radio and unreliable cellular
data**, and the test flow needs it to be in three different network states
at different points (DUT's SoftAP, office WiFi, cellular for MQTT):

- Every SoftAP↔office-WiFi transition needed an operator prompt (join,
  rejoin, or at minimum a checkpoint dialog).
- The phone's cellular-bound MQTT broker connection failed on the clear
  majority of runs this session ("could not connect to broker from phone"),
  blocking the MQTT re-verification steps and, worse, the final reset
  commands — even though **the broker is provably reachable from this office
  network**, since the DUT itself connects to it successfully almost every
  run. The unreliable link is specifically the phone's cellular data, not
  the broker or the network.
- Wireless adb (used for live debugging, unrelated to the test flow itself)
  rides the same WiFi radio and drops every time the phone's WiFi state
  changes for the test.

None of this is fixable by adding more retry logic on the phone — the
phone's radio and cellular data are the actual bottleneck. Moving the
networking onto the jig removes the bottleneck instead of working around it.

## 2. What moves where

| Today (v2.10) | This spec |
|---|---|
| Phone WS-connects to the DUT's SoftAP directly | Jig WS/HTTP-connects to the DUT's SoftAP; phone talks to the jig over USB-serial (already the channel for pulse/relay-sense) |
| Phone's `MqttCommander`/`MqttChecker` connect to the broker over cellular | Jig connects to the broker over the *office WiFi's own internet* -- the same network the DUT already proves works |
| Operator prompted to join/rejoin WiFi at the start, and again at the end | No operator WiFi action anywhere in the flow -- the phone's WiFi state becomes irrelevant to DUT communication entirely (only needed for login/build download, which can happen over cellular any time, decoupled from DUT-specific networking) |
| Jig: pure USB-serial, `PING`/`RELAY?`/`PULSE:<n>` only | Jig: same USB-serial link, plus WiFi STA + HTTP client (to the DUT) + MQTT client (to the broker) |

The jig keeps its existing physical duties (pulse emit on the flow-sim
output, relay dry-contact sense) completely unchanged -- this is additive,
not a rewrite of [`esp32_usb_serial/src/main.cpp`](../../testing/jig_firmware/esp32_usb_serial/src/main.cpp).

## 3. Why this is smaller than it sounds

The two client roles the jig needs already have proven server-side
counterparts in this exact codebase -- this is wiring up matching clients,
not designing new protocols:

- **Local HTTP, not WebSocket.** `LocalServer.cpp` already exposes a plain
  `POST /command` endpoint (`{"cmd": ...}` in, JSON out) as an alternative
  to the WS API, specifically so a client doesn't need a full WebSocket
  stack. ESP32's built-in `HTTPClient` (Arduino core, no extra library)
  covers this -- no WS client library needed on the jig at all.
- **MQTT via PubSubClient**, the exact library `MQTTClient.cpp` already
  uses DUT-side. Same connect/subscribe/publish pattern, same broker.
- The jig's serial command surface stays line-based, same style as
  `PING`/`RELAY?`/`PULSE:<n>` today.

## 4. Jig serial protocol additions

All new commands are synchronous: the phone sends one line, the jig blocks
(HTTPClient/PubSubClient calls are blocking, same simplicity as the
existing sketch -- nothing else time-critical runs concurrently with an
explicit network command mid-flow) until it has a reply or its own internal
timeout, then replies with exactly one line.

| Command (phone → jig) | Reply | Meaning |
|---|---|---|
| `PING` | `PONG` | Liveness (unchanged) |
| `RELAY?` | `RELAY:ON`\|`RELAY:OFF` | Relay sense (unchanged) |
| `PULSE:<n>` | `OK:<n>` | Emit n pulses (unchanged) |
| `JOIN_AP:<ssid>:<pass>` | `JOINED:<ip>` \| `JOIN_FAIL:<reason>` | Join a WiFi network as STA -- used for both the DUT's SoftAP and the office WiFi (same command, different SSID/pass each time) |
| `LEAVE_WIFI` | `OK` | Disconnect STA -- called before switching to a different network |
| `WIFI_STATUS?` | `WIFI:<none\|connected>:<ssid>:<ip>` | Diagnostics -- which network (if any) the jig currently holds |
| `HTTP_CMD:<json>` | `HTTP_OK:<json>` \| `HTTP_FAIL:<reason>` | POST `<json>` to `http://192.168.4.1/command` (only valid while joined to a DUT SoftAP), return the response body |
| `MQTT_CONNECT:<device_id>` | `MQTT_OK` \| `MQTT_FAIL:<reason>` | Connect to the broker (URI/user/pass compiled into the jig firmware, not sent over serial -- see §6) and subscribe to `agrisense/FG1/<device_id>/status`; remembers `device_id` for subsequent `MQTT_CMD` |
| `MQTT_CMD:<json>` | `MQTT_SENT` \| `MQTT_FAIL:<reason>` | Publish `<json>` to `agrisense/FG1/<device_id>/command` (fire-and-forget, matches the broker's own semantics -- no reply payload) |
| `MQTT_STATUS?` | `STATUS:<json>` \| `STATUS_NONE` | Latest retained status message received since `MQTT_CONNECT` (mirrors `MqttCommander.latestStatus()`'s "drain and return freshest" behavior) |

`<json>` payloads are always single-line (no embedded newlines -- standard
JSON serialization already guarantees this), keeping the line-based framing
unambiguous.

## 5. Revised session flow (per unit)

Replaces [`PRODUCTION_TOOL_SPEC_V2.md`](PRODUCTION_TOOL_SPEC_V2.md) §5 steps
4–15. Steps 1–3 (flash, boot log capture over USB) are unchanged -- those
never touched WiFi at all.

| # | Step | Talks to |
|---|---|---|
| 4 | `JOIN_AP:<device_id>:water1234` -- jig joins the DUT's own SoftAP. **No operator action** -- this used to be the first manual WiFi prompt; now the jig does it. | Jig (USB) |
| 5–9 | `HTTP_CMD` for factory_reset, calibrate, relay_test (+ jig's own `RELAY?` sense, unchanged), flow_sensor (+ jig's own `PULSE:<n>`, unchanged), rtc_sync | Jig (USB) → DUT (jig's own WiFi) |
| 10 | `HTTP_CMD:{"cmd":"wifi_config",...}` with the office WiFi's SSID/pass | Jig (USB) → DUT |
| 11 | `LEAVE_WIFI` then `JOIN_AP:<office_ssid>:<office_pass>` -- jig switches itself onto the office network, mirroring what the DUT is doing on its own STA radio | Jig (USB) |
| 12 | `MQTT_CONNECT:<device_id>` | Jig (USB) → broker (jig's WiFi) |
| 13 | WiFi+MQTT evidence: **unchanged** -- still the phone's own live USB-serial capture of the DUT's boot/log output (`captureLiveDutSerial`), since that's independent of any WiFi state on either the phone or the jig. `MQTT_STATUS?` polled through the jig as corroboration, replacing the old cellular-bound `MqttChecker`. | Phone (USB, DUT) + Jig (USB, broker) |
| 14–16 | `MQTT_CMD`/`MQTT_STATUS?` for relay_test_mqtt, flow_sensor_mqtt (+ jig's own `PULSE:<n>`), rtc_mqtt | Jig (USB) → broker |
| 17 | Ship-clean reset: `MQTT_CMD:{"cmd":"factory_reset"}` retried, then `MQTT_CMD:{"cmd":"force_local_mode"}` retried with `MQTT_STATUS?` confirmation -- same reliability logic as today (§ note in v2.10's `MqttCommander.publishCommandWithRetry`), just issued through the jig instead of the phone's own Paho client | Jig (USB) → broker |
| 18 | `LEAVE_WIFI` then `JOIN_AP:<device_id>:water1234` again -- jig rejoins the DUT's now-blank SoftAP. **No operator action** -- this was the second/last manual WiFi prompt; gone too. | Jig (USB) |
| 19 | `HTTP_CMD:{"cmd":"device_info"}` to confirm no WiFi configured, **plus** the phone's own USB-serial capture of the DUT's reboot (unchanged -- still the stronger `wifi_ssid NOT_FOUND` evidence from v2.10) | Jig (USB) → DUT; Phone (USB, DUT) |

Net result: **zero manual WiFi actions anywhere in the per-unit flow.** The
phone's own WiFi/cellular state becomes irrelevant to DUT communication --
only used for login and build download, which don't care when they happen.

## 6. Firmware constants (jig side)

Compiled into the jig firmware, same "single shared value" pattern already
used DUT-side and phone-side -- not sent over serial, since these don't
change per-unit:

```cpp
const char* SOFTAP_PASSWORD = "water1234";     // must match firmware/include/Config.h
const char* MQTT_BROKER_URI = "mqtt.agrisenseandcontrol.in";
const uint16_t MQTT_PORT    = 1883;
const char* MQTT_USER       = "fg1-device";     // must match Config.h / MqttCommander.kt
const char* MQTT_PASS       = "asacfg1";
```

Same caveat already flagged in `PRODUCTION_TOOL_SPEC.md` §9.1 and
`Config.h`'s own TODO: if `SOFTAP_PASSWORD` ever moves to a per-device
derived value, the jig needs to learn that scheme too (e.g. last 4 of MAC,
computable from the SSID itself without a shared secret) -- update all three
places (`Config.h`, `MqttCommander.kt`/`MqttChecker.kt`, this jig firmware)
together, same as today.

## 7. App-side changes

`DutWsClient` and `MqttCommander`/`MqttChecker`'s network transport gets
replaced by a new class -- tentatively `JigNetworkBridge` -- wrapping
`JigSerialClient` with this protocol:

```kotlin
class JigNetworkBridge(private val jig: JigSerialClient) {
    fun joinAp(ssid: String, pass: String): Boolean
    fun leaveWifi()
    fun httpCommand(cmd: String, extra: JSONObject = JSONObject()): JSONObject?
    fun mqttConnect(deviceId: String): Boolean
    fun mqttCommand(cmd: String, extra: JSONObject = JSONObject()): Boolean
    fun mqttLatestStatus(): JSONObject?
}
```

Presenting roughly the same shape as today's `DutWsClient.sendCommand()` /
`MqttCommander.publishCommand()`/`latestStatus()` means `runFunctionalTests()`
mostly swaps call sites rather than being restructured -- the step-by-step
logic (§5 of `PRODUCTION_TOOL_SPEC_V2.md`) doesn't fundamentally change,
only who's holding the network connection at each step.

Net app-side effect is a *simplification*: no more WiFi-state prompts to
build/maintain, no more cellular-network-binding dance
(`ConnectivityManager.bindProcessToNetwork`) that `MqttChecker`/
`MqttCommander` needed specifically to work around the phone's own WiFi
getting in the way of its cellular data.

## 8. Open questions / risks

- **Jig firmware complexity grows for real** -- from a ~120-line sketch to
  something with WiFi STA + HTTPClient + PubSubClient + the existing GPIO
  duties. Still comfortably within one ESP32's resources and built entirely
  from patterns already proven in the DUT's own firmware, but budget real
  dev + bench-validation time, not a quick patch.
- **Jig statefulness**: does it need to track which network it's currently
  on to skip redundant `JOIN_AP` calls, or does the app just always issue
  `LEAVE_WIFI`+`JOIN_AP` explicitly? Recommend the latter -- matches the
  existing stateless, PING-style design philosophy, and avoids a class of
  "jig thinks it's still joined but isn't" bugs for a modest reconnect-time
  cost per phase transition.
- **Timeouts**: `HTTP_CMD`/`MQTT_CONNECT` involve real network round-trips,
  unlike the fixed-timing `PULSE:<n>`. The jig needs its own internal
  timeout (e.g. 8s for HTTP, matching the DUT's own local-command
  responsiveness) so a stuck network call can't hang the phone's serial
  read indefinitely -- reply `..._FAIL:timeout` rather than staying silent.
- **This doesn't eliminate network-switching complexity, it relocates it**
  -- worth being honest about that. The jig now owns the same
  join/reconnect/timeout handling the phone used to juggle. The win is that
  it's dedicated, purpose-built hardware with no cellular dependency and no
  competing concerns (calls, other apps, OS power management), not that the
  complexity disappears outright.

## 9. Build plan

1. Add `JOIN_AP`/`LEAVE_WIFI`/`WIFI_STATUS?`/`HTTP_CMD` to the jig firmware
   first, bench-validate against the SoftAP-phase commands alone (steps
   4–9) before touching MQTT.
2. Add `MQTT_CONNECT`/`MQTT_CMD`/`MQTT_STATUS?`, bench-validate against the
   office-WiFi phase (steps 10–19).
3. Add `JigNetworkBridge` app-side, swap `runFunctionalTests()`'s call
   sites over incrementally (SoftAP phase first, then MQTT phase), keeping
   `DutWsClient`/`MqttCommander` in place until each swapped section is
   bench-confirmed working, so a regression is easy to isolate to one half.
4. Once both halves are validated, remove `DutWsClient`, `MqttCommander`,
   `MqttChecker`, and every WiFi-prompt dialog (`promptEnableHotspot`'s
   successors) from the app entirely.
