#!/usr/bin/env bash
# Build the Lab Station firmware on the server (the secrets live only in the web app's .env).
#   ./build.sh 1.0.0
# Output in ~/agrisense-data/lab/firmware/:
#   labstation.app.bin     app image, sent over WiFi (FOTA)
#   labstation.merged.bin  full image for USB restore from the flasher (padding trimmed)
#   version.txt
set -euo pipefail
VERSION="${1:?usage: build.sh <version>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ENV_FILE="${ENV_FILE:-/var/www/agrisense/webapp/agrisense-webapp/.env}"
OUT="$HOME/agrisense-data/lab/firmware"
WORK="$(mktemp -d)/labstation"
export PATH="$HOME/tools/bin:$PATH"

get() { grep -E "^$1=" "$ENV_FILE" | head -1 | cut -d= -f2- | sed -e 's/^"//' -e 's/"$//'; }
TOKEN="$(get LAB_DEVICE_TOKEN)"; SSID="$(get LAB_HOTSPOT_SSID)"; PASS="$(get LAB_HOTSPOT_PASS)"
[ -n "$TOKEN" ] && [ -n "$SSID" ] && [ -n "$PASS" ] || { echo "LAB_DEVICE_TOKEN, LAB_HOTSPOT_SSID and LAB_HOTSPOT_PASS must be set in $ENV_FILE"; exit 1; }

mkdir -p "$WORK" "$OUT"
cp "$HERE/labstation.ino" "$WORK/"
cat > "$WORK/lab_secrets.h" <<EOF
#define LAB_TOKEN "$TOKEN"
#define LAB_HOTSPOT_SSID "$SSID"
#define LAB_HOTSPOT_PASS "$PASS"
#define LAB_VERSION "$VERSION"
EOF

# min_spiffs: two 1.9 MB app slots (needed for over-the-air updates) + a small file system for the log
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs --jobs 2 --output-dir "$WORK/out" "$WORK" | grep -E "Sketch uses|error" || true
cp "$WORK/out/labstation.ino.bin" "$OUT/labstation.app.bin"
python3 - "$WORK/out/labstation.ino.merged.bin" "$OUT/labstation.merged.bin" <<'PY'
import sys
b = open(sys.argv[1], "rb").read()
end = (len(b.rstrip(b"\xff")) + 3) // 4 * 4
open(sys.argv[2], "wb").write(b[:end])
PY
echo "$VERSION" > "$OUT/version.txt"
rm -rf "$(dirname "$WORK")"
ls -la "$OUT"
