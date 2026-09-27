# TH Monitor — SoftAP version (Group 1)

The board makes its **own WiFi network**, so no router or internet is needed. A phone joins that
network and reads temperature and humidity over HTTP.

## Try it

1. Flash this project (`pio run -t upload`) and power the board.
2. On your phone, join WiFi **`TH-xxxx`** (the last 4 hex digits of the board's MAC; the serial
   monitor prints the exact name). Password: `12345678`.
3. Open **http://192.168.4.1**. The page shows live values and refreshes every 5 s.
4. Tap **Set device clock from this phone** once. The RTC coin cell may be flat.

## API for your app

| Method | Path | Returns |
|---|---|---|
| GET | `/api/now` | `{"id":"TH-addb","ts":"2026-10-01T10:30:00","t":28.4,"h":61.2}` |
| GET | `/api/history` | `[{"ts":…,"t":…,"h":…}, …]`, the last 60 readings (10 min), oldest first |
| POST | `/api/time` | sets the clock; body is plain text `2026-10-01T10:30:00` |

- If the sensor fails, a reading has `"error":"DHT22 read failed"` instead of `t`/`h`.
- Every response includes `Access-Control-Allow-Origin: *`, so browser-based apps can call it too.
- Readings are taken every 10 s (`READ_INTERVAL_S`). History is kept in RAM, so it is lost on reboot.

## Gotchas when building the phone app

- **The phone may ignore this WiFi.** It has no internet, so Android/iOS may send requests over
  mobile data instead. Turn mobile data off while testing, or (Android) bind the app to the WiFi
  network. Ask your AI assistant about `bindProcessToNetwork`.
- **Plain `http://` is blocked by default** on Android (and iOS). Allow cleartext traffic for
  `192.168.4.1`: `usesCleartextTraffic` on Android, an ATS exception on iOS.
- Poll no faster than every 2 s. The DHT22 can't read faster than that anyway.

## Wiring

DHT22 data **D7** · DS3231 SDA **D3** / SCL **D4** · blue LED **D2** (blinks on each reading).

## Stretch goals

- Settings screen: change the reading interval from the app (add a `POST /api/interval`).
- Chart of `/api/history` in the app.
- High/low alerts: the app notifies when `t` or `h` goes out of a range you choose.
