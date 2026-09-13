# USB-OTG Flash Spike

See [SPIKE_SPEC.md](SPIKE_SPEC.md) for what this proves and the go/no-go
criteria it feeds back into the [NB Agri Flasher plan](https://claude.ai/code/artifact/8f1bca38-c2d4-45b3-9431-5f62633308fe).

## What's here

`flasher-spike-app/` — a single-screen native Android app (Kotlin + C/JNI):

- **Transport:** [`mik3y/usb-serial-for-android`](https://github.com/mik3y/usb-serial-for-android) talks to the FG1 board's CP2102 over USB-OTG (`UsbSerialTransport.kt`).
- **Flash protocol:** [`espressif/esp-serial-flasher`](https://github.com/espressif/esp-serial-flasher), vendored as a git submodule at `flasher-spike-app/app/src/main/cpp/esp-serial-flasher`, cross-compiled via the NDK and driven through a custom port (`android_port.c`) that calls back into Kotlin over JNI for the actual USB I/O (see `android_port.h` for why — there's no native USB Host API on Android, only the Java one).

```
UI thread                Background thread              Native (JNI)
MainActivity  ──tap──►  read assets/*.bin
                         UsbSerialTransport.open()
                         NativeFlasher.flash() ────────►  esp_loader_connect_with_stub()
                                                            flash_start/write×N/finish per segment
                          ◄── write/read/setDTR/setRTS ──  (android_port.c calls back per esp-serial-flasher op)
                         result logged, port closed
```

## What's implemented vs. what needs real hardware

Implemented and matches upstream reference code closely (see comments in
`android_port.c` / `jni_bridge.c` pointing at the exact esp-serial-flasher
files each part was ported from):

- USB permission flow (explicit request + auto-launch via `device_filter.xml`)
- The classic esptool DTR/RTS bootloader-entry sequence, ported from
  `esp-serial-flasher/port/linux_port.c`'s `LINUX_GPIO_DTR_RTS` mode
- The connect → flash bootloader/partitions/app → reset sequence, ported
  from `esp-serial-flasher/examples/common/example_common.c`

**Bench-validated as of 2026-09-11** — see [SPIKE_SPEC.md §8](SPIKE_SPEC.md#8-bench-result-2026-09-11--first-successful-phone-native-flash)
for the full story. First real flash succeeded end to end: connect → sync
→ stub upload → flash bootloader/partitions/firmware → MD5 verify → reset,
~57s. Getting there required one real fix beyond the JNI build itself:
`UsbSerialTransport.read()` now buffers — it pulls up to 256 bytes per
underlying driver `read()` call into a queue and hands the SLIP decoder one
byte at a time out of that queue, instead of asking the CP210x driver for
exactly 1 byte per call (which was producing a stuck repeated byte instead
of a real response — confirmed not a hardware/cable issue by a third-party
terminal app reading the same phone+cable+board cleanly).

Still open: SPIKE_SPEC.md §5.3's actual repeatability run (20 consecutive
flashes, ≥18/20 to go) — only one flash has been done so far.

## Bonus: WiFi/SoftAP flash (2026-09-11)

Once the USB path proved out, the same session wired up ElegantOTA on the
FG1 firmware itself (`LocalServer.cpp`, `/update` portal — see
`products/FG1-flowguard/firmware`'s commit history) and added a second
button, **"Flash over WiFi (SoftAP)"**, so a unit already on the bench
doesn't need USB at all: connect the phone to the board's SoftAP, tap the
button, `WifiOtaFlasher.kt` does a plain HTTP multipart upload to
`/ota/upload` (no native code needed — ElegantOTA's own protocol). This
was Avinash's actual pain point (pulling the PCB for every firmware
iteration), not the spike's original subject.

**Status: still unreliable after fixing three real, confirmed bugs across
two more sessions (2026-09-12/13) — don't hand this to Avinash yet.**
Each was root-caused precisely, cross-referenced against community reports
with the identical symptom, and each measurably improved how far a
transfer got before failing:

1. **Task watchdog crash** (`main.cpp`/`LocalServer.cpp`): `Update.write()`'s
   flash erase (up to 64KB blocks, see `Updater.cpp`) runs inside the
   `async_tcp` task's own call stack; a slow erase can block it long enough
   to starve that core's IDLE task past the default ~5s watchdog timeout,
   rebooting mid-transfer. Matches [ESP32Async/AsyncTCP#107](https://github.com/ESP32Async/AsyncTCP/issues/107)
   and [ElegantOTA#47](https://github.com/ayushsharma82/ElegantOTA/issues/47)
   exactly (`task_wdt...Aborting`). Fixed: `disableCore0WDT()`/
   `disableCore1WDT()` (Arduino-ESP32's own standard mitigation) around the
   OTA window.
2. **AsyncTCP ACK timeout** (`platformio.ini`): `CONFIG_ASYNC_TCP_MAX_ACK_TIME`
   defaults to 5000ms — if the ESP32's own transmitted data goes
   unacknowledged that long (exactly what a blocked flash write causes),
   `AsyncWebServerRequest::_onTimeout()` closes the connection outright,
   independent of the watchdog. Raised to 30000 via a build flag.
3. **ESPAsyncWebServer's hardcoded 3s RX timeout** (`scripts/patch_asyncwebserver_rx_timeout.py`):
   set once per connection in the `AsyncWebServer` constructor with no
   build-flag override, cleared only when the response is sent — not
   during a large request body. If no new bytes arrive for 3s (again,
   exactly what a blocked flash write causes on the receiving end), the
   connection is closed. No override hook exists, so this is a PlatformIO
   pre-build script that patches the fetched library source directly
   (durable, idempotent, warns rather than fails if the library changes).

A clean single attempt with all three fixes survived multi-second silent
gaps that would have killed it before, and got measurably further, but
still eventually failed — sometimes past 40%, sometimes with an
`Broken pipe`/`unexpected end of stream` right at 100% written. Also
raised `WifiOtaFlasher.kt`'s client-side `readTimeout` from 30s to 120s,
since it coincidentally matched the just-raised firmware timeouts and
could have been the side actually giving up first.

**New wrinkle, 2026-09-13:** during one stalled attempt, the board's USB
connection to the bench Mac dropped entirely (not just an ESP32 reset —
the whole CP2102 serial device disappeared and had to be reconnected).
That's consistent with a power brownout under sustained SoftAP + flash-
write load, a hardware/power-supply question distinct from all of the
above — not yet confirmed, next thing to rule out (try a different cable/
power source before further firmware changes).

USB-OTG remains the reliable, validated path regardless of how this
resolves.

## Building

```bash
cd flasher-spike-app
./gradlew :app:assembleDebug
```

NDK 27.0.12077973 and CMake 3.22.1 auto-install via the Android SDK license
already on file the first time this runs; no other setup needed beyond the
SDK itself.

Before running, drop real bin files into `app/src/main/assets/` — see
[`app/src/main/assets/README.md`](flasher-spike-app/app/src/main/assets/README.md).

## Known gaps to close before trusting a result

- No progress callback — the native side logs to Logcat (tag `flasherspike`)
  but doesn't report percent-complete back to the UI. Fine for a spike;
  add one before this becomes anything Kamta-facing.
- No retry/backoff around `esp_loader_connect_with_stub()` — a single
  failed sync currently just fails the whole attempt. The repeatability
  test (SPIKE_SPEC.md §5.3) will show whether that matters.
- `esp_loader_connect_with_stub()` was chosen over plain `esp_loader_connect()`
  for speed and reliability (the flasher stub raises the baud rate and
  block size); if it turns out to be less reliable than the ROM-only path
  on this hardware, that's a one-line swap in `jni_bridge.c`.
