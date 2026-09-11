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

**Not yet run against real hardware** — this is scaffolding, not a validated
result. `./gradlew :app:assembleDebug` builds clean end to end (Kotlin,
resources, and the CMake/NDK native module linking against the vendored
`esp-serial-flasher`, for all four ABIs) — see `app/build/outputs/apk/debug/app-debug.apk`.
That resolves half of SPIKE_SPEC.md §4b (the JNI build itself); the real
unknowns left are:

1. Does the DTR/RTS timing actually work against this specific CP2102 +
   Android USB Host stack (the default 100ms/50ms hold times are esptool's
   generic defaults, not verified against FG1's board)?
2. Does `esp_loader_connect_with_stub()` actually sync and flash correctly
   over that link in practice — a clean compile says nothing about runtime
   protocol correctness.

Both require the test plan in SPIKE_SPEC.md §5 on a bench FG1 board.

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
