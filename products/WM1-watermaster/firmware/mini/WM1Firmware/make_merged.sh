#!/bin/zsh
# One image at 0x0 for the remote bench / web flasher:
#   zsh make_merged.sh        -> release/WMM-C3-fw-<version>-merged-0x0.bin
set -e
cd "${0:A:h}"
B=.pio/build/wm_mini_c3
V=$(grep -o 'FIRMWARE_VERSION "[^"]*"' src/Config.h | cut -d'"' -f2)
P=~/.platformio/penv/bin/python
ET=~/.platformio/packages/tool-esptoolpy/esptool.py
BA=~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
mkdir -p release
OUT=release/WMM-C3-fw-$V-merged-0x0.bin
$P $ET --chip esp32c3 merge_bin -o $OUT --flash_mode dio --flash_size 4MB \
  0x0 $B/bootloader.bin 0x8000 $B/partitions.bin 0xe000 $BA 0x10000 $B/firmware.bin >/dev/null
ls -l $OUT | awk '{print $5, $9}'
md5 -q $B/firmware.bin | sed 's/^/app md5 (for ota_url): /'
