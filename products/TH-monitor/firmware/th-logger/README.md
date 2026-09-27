# TH Monitor — Offline logger (Group 2)

This is for a farm with **no internet**. The board saves a reading every minute to a CSV file in
its flash memory, which survives power cuts. It also makes its own WiFi, so when someone walks by
with a phone, the app downloads everything logged since the last visit.

## Try it

1. Flash this project (`pio run -t upload`) and power the board.
2. On your phone, join WiFi **`TH-xxxx`** (password `12345678`) and open **http://192.168.4.1**.
3. Tap **Set device clock from this phone** first. If the RTC coin cell is flat, the timestamps
   are wrong until you do.
4. The page shows live values, how many readings are stored, and a **Download CSV** button.

## API for your app

| Method | Path | Returns |
|---|---|---|
| GET | `/api/now` | `{"id":"TH-addb","ts":"2026-10-01T10:30:00","t":28.4,"h":61.2}` |
| GET | `/api/info` | `{"id":"TH-addb","rows":1234,"bytes":32084,"freeBytes":…,"intervalS":60}` |
| GET | `/api/history?from=TS` | `{"rows":[{"ts":…,"t":…,"h":…}, …],"more":false}`, rows **newer than** `TS`, oldest first, at most 500 |
| GET | `/log.csv` | the whole log as a CSV file: `2026-10-01T10:30:00,28.4,61.2` per line |
| POST | `/api/time` | sets the clock; body is plain text `2026-10-01T10:30:00` |
| POST | `/api/clear` | deletes all stored readings |

### How the app syncs (the core of this project)

```
lastTs = saved in the phone (empty the first time)
repeat:
    GET /api/history?from=lastTs
    save the rows in the phone's database
    lastTs = ts of the last row
until "more" is false
```

Only new rows come over the air, and a sync can stop halfway and resume safely.
Timestamps are ISO text (`2026-10-01T10:30:00`), so comparing them as plain text also compares
them in time.

## Numbers

- 1 row per minute ≈ 30 bytes → **~37 KB per day**.
- The log is capped at 1 MB (**~27 days**). When full, the oldest half is dropped.
- The flash has 2 MB for files. Change `LOG_INTERVAL_S` / `MAX_LOG_BYTES` in `main.cpp` if needed.
- The board also saves a row at every power-on, so short gaps show where the power was off.

## Gotchas when building the phone app

The same as the SoftAP version: turn off mobile data while testing (or bind the app to the
WiFi network), and allow cleartext `http://` for `192.168.4.1`.

## Serial monitor (115200)

Every saved row is printed. `D` dumps the whole log. `T 2026-10-01 10:30:00` sets the clock.

## Instructor option

Build with `PLATFORMIO_BUILD_FLAGS=-DALSO_JOIN_SAVED_WIFI` and the board also joins the router
WiFi it last saved (e.g. from the MQTT version). A laptop on that network can then `curl` the API
at the board's LAN IP.

## Wiring

DHT22 data **D7** · DS3231 SDA **D3** / SCL **D4** · blue LED **D2** (blinks when a row is saved).

## Stretch goals

- Charts in the app: daily min/max, and the last 7 days.
- Sync automatically when the phone joins `TH-xxxx`.
- Export from the app to Excel / Google Sheets, or share on WhatsApp.
- Let the app change the logging interval (`POST /api/interval`, saved in a file on the board).
- Battery: sleep between readings and wake the WiFi only when a button is pressed.
