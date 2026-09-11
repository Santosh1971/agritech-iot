# Bench bin files (not committed)

Drop these here before running the app (any subset — the app flashes whichever are present):

- `bootloader.bin`
- `partitions.bin`
- `firmware.bin`

Source, per [SPIKE_SPEC.md §3](../../../../../SPIKE_SPEC.md): the `esp32dev_ds1307` bench
build, not a field DS3231 build —

```bash
cd products/FG1-flowguard/firmware
pio run -e esp32dev_ds1307
cp .pio/build/esp32dev_ds1307/{bootloader,partitions,firmware}.bin \
   ../../projects/tools/usb-otg-flash-spike/flasher-spike-app/app/src/main/assets/
```

These are gitignored (`*.bin` under this folder) — same convention as
`products/FG1-flowguard/firmware/.gitignore`, which excludes `.pio` build
output. Don't commit a compiled firmware binary here.
