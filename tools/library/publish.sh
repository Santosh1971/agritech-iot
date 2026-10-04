#!/usr/bin/env bash
# Upload the document library (index.json + files/) to the website server.
# The files are internal, so they live outside the public repo, in
# ~/agrisense-data/library on the server; /dashboard/library lists them for admins.
#
# Usage: tools/library/publish.sh <folder containing index.json and files/>
set -euo pipefail
SRC="${1:?folder with index.json and files/}"
HOST="santosh@87.76.191.135"
KEY="$HOME/.ssh/agritech_key"

ssh -i "$KEY" "$HOST" 'mkdir -p ~/agrisense-data/library/files'
rsync -av --progress -e "ssh -i $KEY" "$SRC/files/" "$HOST:agrisense-data/library/files/"
rsync -av -e "ssh -i $KEY" "$SRC/index.json" "$HOST:agrisense-data/library/index.json"
echo "Published. Open https://agrisenseandcontrol.in/dashboard/library"
