# WM-Mini C3 firmware (WM1-Mini firmware 0.4.0, `env:wm_mini_c3`)

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

## Firmware update over WiFi (0.4.0)

Password: `~/.config/agrisense/wmm_ota_pass` (or env `WMM_OTA_PASS`), compiled in by
`wmm_secrets.py`; never in this repo. Without it, updates over WiFi are disabled.

- **Same WiFi as the board (push):** `python3 ../tools/wmm_ota.py <board-ip>`
- **From anywhere (pull):** the board downloads the image itself.
  `python3 ../tools/wmm_cloud.py <WM1_id> ota <url> <md5>`. The URL must be private
  (the image contains the update password and broker login): not a public GitHub link.

The image goes to the spare slot; the board switches only after the whole image
arrived and its MD5 matched. All relays switch off while an update is written.

### Tested 2026-11-18 on an FG1 C3 TH controller (WM1_EE126AF8)

Boot, RTC set and kept through reset, valve on -> pump on (interlock), doser,
all off; cloud status and commands through the broker; push update 0.4.0 ->
0.4.1-test (1.13 MB, MD5 checked); pull update back to 0.4.0 sent through the
cloud (file served on the LAN); wrong key refused. Fixed on the way: the flow
input floated on that carrier (~800 false pulses/s, now INPUT_PULLUP), and an
upload cut off mid-way left Update running so the next one failed with "No
Error" (now cleared first, and aborted when the PC disconnects).

## Remote testing (developer away from the board)

`../tools/wmm_cloud.py <WM1_id> status | watch | cmd '<json>' | ota <url> <md5>`
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
