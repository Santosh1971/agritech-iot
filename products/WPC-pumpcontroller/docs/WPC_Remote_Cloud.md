# WPC — Remote Monitoring & Control over the Internet (v0.4 firmware / app v2)

**Requested by Kamta, Sep 2026:** the operator should be able to monitor and control the pumps remotely, not only from the Master's own WiFi.

**Scope of the change:** Master firmware and the app only. **The Pump Node firmware is unchanged for this feature** — pumps still talk LoRa to the Master and nothing else, so a remote command is simply a command the Master would otherwise have received from the local app.

## Decisions taken (and what was deliberately left out)

| Topic | Decision |
|---|---|
| Internet path | **Farm WiFi only.** The Master joins the farm router as a WiFi client while still running its own SoftAP. No GSM/4G hardware. |
| Broker & credentials | Same shared broker and **FG1's credential** (`mqtt.agrisenseandcontrol.in:1883`, plain MQTT). **Security hardening is deferred by decision** — see "Security" below for what that means. |
| Who may control | **Anyone using the app.** Several people operate one installation; whoever switches a pump ON, anyone else can switch it OFF. No owner, no lock. |
| Manual override expiry | **None.** Remote and local overrides stay until someone sets the pump back to Auto (still cleared by a Master reboot, as before). |
| Several installations | A user can keep a list of Masters (different farms) and switch between them. |

## Architecture

```
 phone (any network) ──MQTT──►  broker  ◄──MQTT──  Master (farm WiFi)  ──LoRa──►  Pump Nodes
 phone (on Master's WiFi) ────────────HTTP────────────►  Master (SoftAP)
```

Both paths end in **the same command functions** on the Master (`cmdOverride`, `cmdAssign`, `cmdSetName`, `cmdForget`, `cmdSetConfig` in `master_node/src/main.cpp`), so remote control cannot behave differently from local control. The status JSON is also the same, built by one function (`buildStatusJson()`), used by `GET /status`, the MQTT status topic and the serial console.

### Topics (mirror WM1/FG1)

Device ID is `WPC_<masterId>`, where `<masterId>` is the same 8 hex characters used in the SoftAP name (`WPC-Master-68A99B20` → `WPC_68A99B20`).

| Topic | Direction | Retained | Content |
|---|---|---|---|
| `agrisense/WPC/WPC_<id>/status` | Master → cloud | yes | Same JSON as `GET /status`. Published on change (at most 1/s) and at least every 10 s. |
| `agrisense/WPC/WPC_<id>/lwt` | Master → cloud | yes | `{"online":true}` on connect; broker publishes `{"online":false}` if the Master drops. |
| `agrisense/WPC/WPC_<id>/command` | app → Master | **no** | `{"cmd": ..., ...}`, see below. Never retained, so a command can never replay when the Master reconnects. |

### Commands (`command` topic)

| `cmd` | Fields | Same as local |
|---|---|---|
| `override` | `slot`, `enabled`, `state` | `POST /override` |
| `assign` | `slot`, `level`, `assigned` | `POST /assign` |
| `set_name` | `slot`, `name` | `POST /name` |
| `forget` | `slot` | `POST /forget` |
| `set_config` | any of `numLevels`, `debounceMs`, `txPower` | `POST /config` |

**Not available remotely on purpose:** setting the Master's WiFi (`POST /wifi`) and everything on a Pump Node's own SoftAP (identity, target Master, its TX power). WiFi changes are local-only so nobody can knock an installation off the internet from afar; Pump provisioning needs to be next to the Pump anyway.

### Latency

A remote override is a *state change*, so it jumps the Master's poll queue (see `WPC_LoRa_Protocol_v0.3.md` §5), but it still waits for the pacing gap already in progress. **Measured on the bench** (Master + Pump, console override → Pump relay): about **4–5.5 s** in normal mode (one 5 s gap plus the exchange), 1.4 s in `TESTMODE`. Add the phone → broker → Master hop for cloud commands (not yet measured).

## Master firmware

- **`src/Cloud.h`** (`CloudLink`): WiFi STA + MQTT client. Runs in its **own FreeRTOS task on core 0**, because DNS lookups, TCP connects and MQTT keep-alives can block for seconds while the Pump fail-safe is 60 s — nothing network-related may ever stall the LoRa loop. The task and the main loop only exchange data through a command queue (task → loop) and a mutex-protected status string (loop → task); `pumps[]`, NVS and the radio are only ever touched from the main loop.
- **WiFi mode** is AP+STA. The local SoftAP and HTTP API are unchanged.
- **Credentials** are stored in NVS (`wifiSsid`, `wifiPass`) and set with `POST /wifi {"ssid","password"}` (empty SSID clears), or the serial console (`WIFI "name with spaces" password`). The app offers a scanned list of networks to pick from (`/wifi/scan`, see the protocol doc); SSIDs up to 32 bytes with spaces are supported.
- **`backgroundService()`** replaces the bare `server.handleClient()` calls inside the blocking LoRa waits: it now also drains cloud commands, services the serial console and hands the status snapshot to the cloud task, so remote commands are honoured even mid-poll.
- **`/status` additions:** `fw`, `wifi{configured,ssid,connected,ip}`, `cloud` (MQTT up).
- **Firmware version** (`FW_VERSION`) is new, also reported by the Pump's `/info`.

### Known limitations / things to test in the field

1. **AP+STA channel coupling.** An ESP32 running a SoftAP and a WiFi client at once must use the router's channel for both. If the router changes channel, or the STA reconnects on a different one, the Master's own SoftAP can drop for a few seconds and the phone may disconnect. STA reconnect attempts are therefore spaced 30 s apart. Worth testing on a real farm router.
2. **Retained status can be stale.** If the Master loses power or WiFi, the broker still holds its last status. The app uses the `lwt` topic and shows an "offline — last known state" banner instead of presenting it as live; commands are refused while the Master is offline.
3. **The status refreshes only when the Master polls.** IN1/IN4 telemetry and online/offline state are only as fresh as the Master's round-robin (about N × 5 s for N pumps), remote or local.
4. **Android routing.** With mobile data on, Android may send traffic for `192.168.4.1` over mobile data instead of the (internet-less) Master WiFi. Local mode has worked so far; if it misbehaves on some phones, WM1's app has a `NetworkBinding` helper for this that could be ported.

## Security (deferred — what this means today)

The shared credential and topic names are compiled into the Master firmware **and** the APK, on plain port 1883. Anyone who extracts either can read and command *any* WPC's topics, since the topic is just the Master ID printed in its WiFi name. Consistent with the current decision this is accepted for now; when it is tackled, the natural steps are TLS on 8883, per-Master credentials with broker ACLs (`agrisense/WPC/WPC_<id>/#` only), and an owner/claim step per installation.

## Broker credential

The Master, the app and the test script use **FG1's existing credential** (`fg1-device` / `asacfg1`, defined in `Cloud.h` and `backend.dart`). It was checked against the live broker: it connects, may subscribe to `agrisense/WPC/#`, and a publish-and-receive round trip on a probe topic under `agrisense/WPC/` worked, so **no broker changes are needed**. A dedicated `wpc-device` user was tried first and does not exist there (the broker answered "not authorised").

Consequence to be aware of: because the broker lets this credential use every `agrisense/#` topic it was tested on, any FG1 credential holder can also read and command WPC topics, and vice versa. That is the deferred-security trade-off already accepted (see "Security" above).

## App (v2)

See `WPC_Mobile_App.md`. Summary: `api.dart` routes every Master call over local HTTP or MQTT depending on the mode, so the Status and Assign screens are unchanged; a new **Connection** screen (top-right button) holds Local/Cloud mode, the list of installations, and (while on the Master's WiFi) the farm-WiFi setup.

## Serial console (both nodes)

Line-based, replies prefixed `@OK` / `@ERR` / `@DATA`. Used by the test jig and handy for bring-up. Master: `ID STATE INPUTS PUMPS ASSIGN OVERRIDE FORGETALL TESTMODE TXPOWER WIFI WIFISTAT LEDTEST FACTORYRESET REBOOT`. Pump: `ID STATE ADC RELAY MASTER TXPOWER TESTMODE LEDTEST FACTORYRESET REBOOT`. `TESTMODE 1` shortens timings (Master: 300 ms level debounce, 1 s poll gap; Pump: 8 s fail-safe) for the jig and is never persisted. Details in `testjig/README.md`.
