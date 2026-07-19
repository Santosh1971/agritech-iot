# SWC SoftAP Simulator

Mock ESP32 SoftAP server so the Flutter app's SoftAP flow can be built and tested
with zero hardware. Final verification still happens on real ESP32.

## What it does

- Serves the same-shaped HTTP endpoints your ESP32 SoftAP config server does
  (`/status`, `/wifi/config`, `/command/relay`, `/cycles`, `/leds`)
- Pushes live state over a WebSocket at `/ws` (relay, flow rate, WiFi LED, flow LED)
- Serves a browser control panel at `/` so you can flip the relay, inject flow
  pulses, and change WiFi state manually while watching the Flutter app react

**Adjust the route table in `server.js`** once you confirm your firmware's actual
SoftAP HTTP paths/payloads — the state machine and dashboard don't need to change.

## Setup — office laptop (today)

```bash
cd ~/Projects/SmartWaterController   # or wherever you keep the repo
mkdir simulator                       # skip if you place these files directly
# copy server.js, state.js, package.json, README.md, public/dashboard.html in here
cd simulator
npm install
npm start
```

Open `http://localhost:4000` in a browser — you'll see the control panel.
Point the Flutter app's SoftAP base URL at `http://localhost:4000` (or your
machine's LAN IP if testing from a real phone, e.g. `http://192.168.1.x:4000`).

Commit and push so it's on GitHub for tonight:

```bash
git add simulator/
git commit -m "Add SoftAP simulator for hardware-free app development"
git push
```

## Setup — Mac (this evening)

```bash
cd ~/Projects/SmartWaterController
git pull
cd simulator
npm install
npm start
```

Node v20 is already on your Mac, so no version juggling needed.

## Flutter app side

Add a simulator toggle (e.g. in Settings/BLE Setup screen or a debug flag) that
swaps the SoftAP base URL:

```dart
final softApBaseUrl = useSimulator
    ? 'http://localhost:4000'      // or LAN IP from a real phone
    : 'http://192.168.4.1';        // real ESP32 SoftAP
```

Everything else in `BleService`/HTTP client code stays the same — only the
base URL changes.

## Limitations (still need real hardware for)

- RTC drift, brownout resets, real WiFi reconnect latency at weak RSSI
- Relay chatter, flow sensor pulse noise/debounce
- Power-failure-mid-NVS-write corruption scenarios
- SoftAP RF quirks: client limits, DNS captive-portal behavior, range/interference
- If provisioning also uses BLE anywhere, that can't be simulated in a browser —
  would need a separate native mock BLE peripheral

This tool is for validating **app-side protocol logic and UI flow** fast. The
hardware pass at the end is still non-negotiable for the above.
