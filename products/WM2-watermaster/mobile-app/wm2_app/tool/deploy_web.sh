#!/usr/bin/env bash
# Build the WM2 web app and upload it to agrisenseandcontrol.in/app/wm2.
# The web app talks only to our server: it must NOT contain the broker
# password (checked below). Next.js only sees new files in public/ when it
# starts, so the site is restarted at the end (no rebuild).
set -euo pipefail
cd "$(dirname "$0")/.."
SERVER=santosh@87.76.191.135
KEY="$HOME/.ssh/agritech_key"
DEST=/var/www/agrisense/webapp/agrisense-webapp/public/app/wm2/

flutter build web --release --base-href /app/wm2/ --pwa-strategy=none
cp tool/sw_cleanup.js build/web/flutter_service_worker.js
if ! tail -n 3 build/web/flutter_bootstrap.js | grep -q "_flutter.loader.load();"; then
  echo "flutter_bootstrap.js does not end with a plain load() (service worker?): stop" >&2; exit 1
fi
PASSFILE="$HOME/.config/agrisense/wm2_mqtt_pass"
if [ -f "$PASSFILE" ] && grep -qF "$(cat "$PASSFILE")" build/web/main.dart.js; then
  echo "broker password found in the web build: stop" >&2; exit 1
fi
[ "${1:-}" = "--build-only" ] && { echo "built, not uploaded"; exit 0; }
ssh -i "$KEY" "$SERVER" "mkdir -p $DEST"
rsync -az --delete -e "ssh -i $KEY" build/web/ "$SERVER:$DEST"
ssh -i "$KEY" "$SERVER" 'pm2 restart agrisense-web >/dev/null && sleep 5 && pm2 jlist | python3 -c "import sys,json; [print(p[\"name\"], p[\"pm2_env\"][\"status\"]) for p in json.load(sys.stdin) if p[\"name\"]==\"agrisense-web\"]"'
for u in /app/wm2 /app/wm2/main.dart.js; do
  echo "$u -> $(curl -s -o /dev/null -w '%{http_code}' "https://agrisenseandcontrol.in$u")"
done
