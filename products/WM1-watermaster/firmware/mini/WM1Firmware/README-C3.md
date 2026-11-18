# WM-Mini C3 firmware (WM1-Mini firmware 0.4.1, `env:wm_mini_c3`)

The WM1 Water Manager-Mini firmware with a second board build for **WM-Mini C3**
(ESP32-C3 Super Mini, six relays on GPIOs through a ULN2003; hardware in
`products/WM1-watermaster/hardware/WM-MiniC3`). Same scheduler, programs, sequences,
dosing interlock, history, WiFi/SoftAP, cloud topics and app as WM1-Mini, so the
**WM1 Mini app (1.0.2) works with it unchanged**. Pins are in `src/Board.h`.

| Function | WM-Mini C3 pin | On the FG1 / ASC Mini-C3 test board |
|---|---|---|
| RL1 pump | IO20 | its relay (real) |
| RL2 doser | IO10 | STATUS LED |
| RL3-RL6 valves V1-V4 | IO1, IO0, IO3, IO4 | V3 = its 5 V pump output + LED; V1, V2, V4 only in the app/log |
| Flow input | IO5 | real flow input |
| DS1307 SDA/SCL | IO6 / IO7 | real RTC |
| WiFi LED | IO8 (active LOW) | same |
| Flow LED | IO21 | not wired (the board's flow LED blinks in hardware) |

No pressure, battery or water-level inputs on this board: they read 0, and the
water-level check can never block a run.

## Build and flash (USB)

```bash
cd products/WM1-watermaster/firmware/mini/WM1Firmware
pio run -e wm_mini_c3 -t upload        # if it can't connect: hold BOOT while plugging in USB
pio device monitor -e wm_mini_c3
zsh make_merged.sh                     # release/WMM-C3-fw-<v>-merged-0x0.bin for the remote bench
```

## Firmware update over WiFi (0.4.1)

Password: `~/.config/agrisense/wmm_ota_pass` (or env `WMM_OTA_PASS`), compiled in by
`wmm_secrets.py`; never in this repo. Without it, updates over WiFi are disabled.

- **Same WiFi as the board (push):** `python3 ../tools/wmm_ota.py <board-ip>`
  (HTTP basic auth, MD5 required, one upload at a time).
- **From anywhere (pull):** `python3 ../tools/wmm_cloud.py <WM1_id> ota <url> <local .bin>`.
  The board downloads the image itself. The password never travels: the tool reads
  the board's one-time `ota_nonce` from its status and sends
  `mac = HMAC-SHA256(password, nonce|url|sha256)` with the file's SHA-256. The nonce
  changes after every attempt (a command seen on the broker cannot be replayed), and
  the downloaded image must hash to the signed SHA-256 or it is thrown away.

The image goes to the spare slot; the board switches only after the whole image
arrived and was checked. All relays switch off while an update is written.
Tested 2026-11-18: good signed pull worked; replay, wrong signature and a swapped
file were refused with the running firmware kept.

Known limits: the broker is plain MQTT on port 1883 with the shared WM1 login, so
anyone with that login can read status and send ordinary commands (as for every
WM1 today); only firmware updates are signed. The local push uses one shared
password over plain HTTP on the farm WiFi.

The NB Agri Flasher cannot do the first flash of a blank C3 (it writes the
bootloader at 0x1000, the ESP32 address; a C3 boots from 0x0), so a new board is
first flashed through the remote bench with the merged 0x0 image.

## Remote testing (developer away from the board)

`../tools/wmm_cloud.py <WM1_id> status | watch | cmd '<json>' | ota <url> <local .bin>`
reads and commands the board through the AgriSense broker (login wm1-device,
`~/.config/agrisense/wm1_mqtt_pass`). The board's status now carries `fw`, `board`,
`uptime_s`, `ip` and `ota`. Serial log and USB flashing go through the remote bench
(`tools/bench/bench.py`, tester's browser).

### Tester's setup (Avinash, C3 board at home)

1. Plug the board into the PC by USB. Open
   `agrisenseandcontrol.in/dashboard/testing/bench` in Chrome or Edge, click
   **Connect board**, and keep the page open while we test.
2. First flash comes from the developer through that page. If it does not start:
   unplug, hold **BOOT**, plug in, release BOOT, then press Start again.
3. On the phone, join WiFi `WM1_xxxxxxxx` (the WM1 SoftAP password), open the
   WM1 Mini app, Local Setup: set the home WiFi and sync the time from the phone.
4. Flow sensor on the flow connector; coin cell in. **No 230 V at home**: relay
   click and LEDs are enough.
5. Tell the developer the device ID (`WM1_...`, shown in the app). After that,
   updates come over WiFi and the PC is needed only for the serial log.
