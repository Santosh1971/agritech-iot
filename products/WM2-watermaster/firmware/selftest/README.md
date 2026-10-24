# WM2-WaterMaster self-test

Bench firmware that checks each block of the WM2-WaterMaster board and prints
PASS / WARN / FAIL over serial. Not product logic.

## Flash it

One file, written at address **0x0** (contains bootloader + partitions + app):

`release/WM2-selftest-0.1-merged-0x0.bin`  (`.bin` files are gitignored; rebuild with the steps below)

Any of these works. Close every serial monitor first, since only one program can use the port.

| Tool | How |
|---|---|
| Espressif Flash Download Tool (Windows) | ChipType ESP32, add the file at `0x0`, SPI 40MHz / DIO, COM port, baud 460800, START |
| Web flasher: https://espressif.github.io/esptool-js/ (Chrome) | Connect, add file at offset `0x0`, Program |
| esptool | `esptool.py --chip esp32 -p PORT -b 460800 write_flash 0x0 WM2-selftest-0.1-merged-0x0.bin` |

If the tool can't connect: hold **BOOT** on the programmer, tap **RESET**, release BOOT, retry.
After flashing, tap **RESET** to run.

The app-only `release/WM2-selftest-0.1-app-0x10000.bin` is for boards that already have a
bootloader (e.g. later updates through NB Agri Flasher). A brand-new module needs the merged file.

## Read the result

Open a serial terminal at **115200** baud (PuTTY, Arduino Serial Monitor, `pio device monitor`).
The test runs once at boot and ends with a summary. Press `h` for the menu.

### Minimal board (module + 3.3V regulator only)

This is all that's needed to prove a new module is good:

| Line | Expected | Means |
|---|---|---|
| MCU | **PASS**, `4 MB flash` | Module and its flash are good |
| WiFi | **PASS**, networks listed | Radio and antenna good |
| 12V monitor | WARN `no 12V input` | Normal, divider not fitted |
| RTC | FAIL `U5 not fitted yet` | Normal |
| Flow inputs | WARN (random) | Normal, pull-ups not fitted |
| Valve drivers | SKIP `needs 12V` | Normal |

If nothing at all prints, or the flasher reports `Failed to communicate with the flash chip` /
flash ID `ff/ffff`, the module itself is faulty.

### Full board

Everything should be PASS, apart from:
- **Valve drivers:** shows WARN until you've watched the green/yellow LED pairs flash both ways.
- **12V monitor:** shows WARN when running from programmer 5V only.

Menu commands:

| Key | Action |
|---|---|
| `a` | Re-run all tests |
| `l` | LED walk |
| `m` | Live flow Hz + pressure switch (any key stops) |
| `b` | 12V reading |
| `r` / `s` | Read / set RTC |
| `w` | WiFi scan |
| `v` | Valve test |
| `1`–`4` | Single valve pulse (V1 fwd / V1 rev / V2 fwd / V2 rev) |

## Build from source

```bash
cd products/WM2-watermaster/firmware/selftest
pio run -t upload        # flash over /dev/cu.usbserial-0001
pio device monitor
```

Pin map: `../common/wm2_pins.h` (from the KiCad schematic, shared with the main firmware).
