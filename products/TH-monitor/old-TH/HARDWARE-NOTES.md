# Old TH Monitor (Sasya) — hardware connections & findings

Source: `fixed.zip` (Girisan R, cleaned up Oct 2025). `baselib/` = common Sasya controller
library, `thmonitor/` = TH app. The original `.git` histories and `.pio` build output were not
copied into this repo; the zip in Downloads still has them.

## Board

- Wemos **D1 mini** (ESP8266, 4 MB flash) on a carrier PCB, named `5x5-WemosRTC-Master` in
  the SystemInfo payload.
- **DHT22** (temperature/humidity) is read with the `robtillaart/DHTNEW` library.
- **DS3231** RTC (address 0x68) is read with `adafruit/RTClib` `RTC_DS3231`, on I2C.
- **IP5306** battery/boost IC (address 0x75), on the same I2C bus. At boot the firmware writes
  `0x37` to register `0x00` ("boost keep-on"). Without this, the IP5306 turns its output off
  when the load is light.

## Pin map

| Function | v03.04 source (this zip, May 2022) | v03.07 build on the board (Oct 2022) |
|---|---|---|
| I2C SDA (RTC + IP5306) | **D6** / GPIO12 | **D3** / GPIO0 (boot log `Wire.begin 0 2`) |
| I2C SCL | **D5** / GPIO14 | **D4** / GPIO2 |
| DHT22 data | **D7** / GPIO13 (`DHT22_PIN`) | unknown (source not available) |
| WiFi status LED (blue) | **D2** / GPIO4 (`-DLED_PIN_BLUE=4`) | **D2** / GPIO4 (boot log `LED::initialize 4`) |

The pins differ between the two versions, so the carrier PCB was probably revised between
them. Which mapping is right for a given board has to be checked on the board itself.

**Confirmed on unit 98cdac310f6d (battery on, 2026-09-27):** DS3231 answers at 0x68 with
**SDA = D3, SCL = D4**, so this board uses the v03.07 mapping. No IP5306 at 0x75: probably the
variant without I2C, so the keep-on write can't work. No 0x57 EEPROM. The DHT22 answered on
**no** pin (D0–D8 and RX), with either DHT22 or DHT11 timing. The sensor or its wiring needs a
physical check.

**Continuity-checked by Santosh (2026-09-27):** DHT22 data = **D7**, SDA = **D3**, SCL = **D4**,
blue LED = **D2**. A line probe on D7 shows the line idles HIGH (external pull-up present) but
the sensor never answers a start pulse. So the fault is the sensor's VCC/GND or the sensor itself.
The DS3231 reported `lostPower`, so its coin cell is probably missing or flat.

Pin-finder observations on the board (USB power only, before the battery was switched on):
- No I2C device answered on any pin pair, and the DHT22 timed out on every pin.
- **D3 reads LOW when idle.** This fits I2C SDA on D3 with the RTC module's pull-up resistors
  going to an *unpowered* rail, which drags the line low. So the sensor rail looks unpowered
  on USB; it probably comes from the IP5306 output (battery + power button).
- D8 reads LOW when idle, which is expected: the D1 mini has a pull-down on GPIO15.

Pins the code defines but does not use on this build (TTGO/ESP32 GSM variant): MODEM_*,
LoRa_*, BATT_V=35, Relay_CH1/2, LED_PIN_RED/YELLOW.

## Behaviour (v03.04 source)

- Reads T/H every `TempInterval` seconds (default 60; the board's EEPROM had 1800/900).
- Readings go to SPIFFS `/data/TH.log` and are sent over MQTT. If a send fails, the reading is
  stored and sent later with `live:0`.
- MQTT topics: `mqtt_tx/TH/<deviceId>` and `mqtt_rx/TH/<deviceId>`.
- Broker: `testbroker.sasyasystems.com`. Firmware update (FOTA) server: `http://fota.sasyasystems.com`.
- WiFi setup uses a WiFiManager portal. The preset AP is `Sasya` (from the boot log).
- The TH payload looks like:
  `{"payloadId":"TH","Temp":29.9,"Humi":75,"ts":"…Z","seqNum":0,"live":1,"Date":"…Z","mac":"…"}`
- MQTT commands: `GetTH`, `SetParameter` (TempInterval, StoreLimit), `SetTime`, `SystemReset`,
  `SystemUpgrade`, `SystemInfo`.
- Serial console commands at 115200: `r` restart, `W [ssid pass]` WiFi, `T 0|1|2` topic
  stage, `E`/`e` EEPROM, `Q <json>` mock MQTT message.
- v03.07 (on the board) adds deep sleep (`Platform::deepsleep`, max 1810 s between wakes).

Full details: `thmonitor/Documents/ProductReference.md`.

## Backup of the board's original flash

`../firmware/original-backup/sasya_thmonitor_<mac>_4MB.bin (one per unit)` holds v03.07. It was verified against the
chip's MD5 and is gitignored. Restore with:
`esptool.py --port <port> write_flash 0 sasya_thmonitor_<mac>_4MB.bin`
