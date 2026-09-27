# TH Monitor — MQTT version (Group 3)

The board joins your WiFi, sets its clock from the internet, and publishes a reading every 30 s
to a public MQTT broker. Your app subscribes to the same broker from anywhere.

## Try it

1. Flash this project (`pio run -t upload`) and power the board.
2. **First start:** the board opens WiFi **`TH-xxxx`** (password `12345678`). Join it from a
   phone. A setup page opens (or go to http://192.168.4.1). Tap *Configure WiFi*, pick your
   WiFi and enter its password. The board remembers it.
3. Open `web-client.html` in a browser, type the device ID (`TH-xxxx`) and watch the readings.
   You can also watch from a terminal:
   `mosquitto_sub -h broker.emqx.io -t 'agrisense/th/TH-xxxx/#' -v`

## Topics

| Topic | Direction | Payload |
|---|---|---|
| `agrisense/th/TH-xxxx/data` | board → app (retained) | `{"id":"TH-xxxx","ts":"2026-10-01T10:30:00","t":28.4,"h":61.2}` |
| `agrisense/th/TH-xxxx/status` | board → app (retained) | `online`, or `offline` (sent by the broker if the board drops off) |
| `agrisense/th/TH-xxxx/cmd` | app → board | `read` · `interval 30` · `time 2026-10-01T10:30:00` |

The `interval` setting goes back to 30 s after a reboot. Saving it in flash is a stretch goal.

*Retained* means the broker keeps the last message, so a newly opened app shows a value at once.

## Broker

- Firmware: `broker.emqx.io`, port **1883** (plain TCP).
- Apps and browsers: `wss://broker.emqx.io:8084/mqtt` (MQTT over WebSocket).
- Libraries: `mqtt` (npm) for React Native/Expo/web, `mqtt_client` for Flutter.
- **It is public:** anyone who knows your topic can read it or send commands. That's fine for
  learning. Real products use a private broker with a username/password or TLS (a stretch goal).

## Serial monitor (115200)

Every reading is printed as JSON. `W` forgets the WiFi and reopens the setup page.
`T 2026-10-01 10:30:00` sets the clock by hand.

## Wiring

DHT22 data **D7** · DS3231 SDA **D3** / SCL **D4** · blue LED **D2** (on = connected to the
broker, short blink = reading sent).

## Stretch goals

- History chart in the app: keep every message the app receives, or save them to a database.
- Alerts: push a phone notification when `t` or `h` crosses a limit.
- Private broker: HiveMQ Cloud or EMQX Cloud free tier, with a username/password and TLS
  (port 8883).
- Offline buffer: store readings on the board while WiFi is down and send them later with
  `"live":0` (the old Sasya firmware did this).
