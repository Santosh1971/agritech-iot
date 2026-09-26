# Bundled bootloader/partitions (not committed)

Unlike flasher-spike-app's assets (which can hold all three segments
for ad hoc bench flashing), this app *always* needs exactly these two
here -- the app/firmware segment itself always comes fresh from the NB
Agri Flasher backend per the product/build the operator picks (see
ApiClient.kt), never bundled:

- `bootloader.bin`
- `partitions.bin`

These two barely ever change (only if the ESP-IDF toolchain version or
the partition table itself changes) -- bundling them locally avoids
needing a laptop/backend round trip for something that's effectively
constant, while the actual application firmware (which changes with
every release) is always fetched fresh and version-selected.

Regenerate after a partition-table or toolchain change:

```bash
cd products/FG1-flowguard/firmware
pio run -e esp32dev_ds1307
cp .pio/build/esp32dev_ds1307/{bootloader,partitions}.bin \
   ../../projects/tools/flasher-tester-app/app/src/main/assets/
```

Gitignored (`*.bin` under this folder) -- don't commit compiled binaries here.
