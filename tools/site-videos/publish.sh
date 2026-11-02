#!/bin/bash
# Upload the product videos shown on agrisenseandcontrol.in/products/<slug>
# to the VPS (~/agrisense-data/site-videos). The MP4s are rendered by each
# product's marketing/ad Remotion project and are not committed (too large).
# Which video appears on which page is set in webapp/agrisense-webapp/lib/site/products.ts.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
P="$ROOT/products"
FILES=(
  "$P"/FG1-flowguard/marketing/ad/out/{fg1-explainer-en,fg1-explainer-hi,fg1-ad-vertical}.mp4
  "$P"/WPC-pumpcontroller/marketing/ad/out/{wpc-explainer-en,wpc-explainer-hi,wpc-user-manual-en,wpc-user-manual-hi,wpc-ad-vertical}.mp4
  "$P"/WM1-watermaster/marketing/ad/out/{wm-explainer-en,wm-explainer-hi}.mp4
)
ssh -i ~/.ssh/agritech_key santosh@87.76.191.135 'mkdir -p ~/agrisense-data/site-videos'
rsync -avz --progress -e "ssh -i $HOME/.ssh/agritech_key" "${FILES[@]}" santosh@87.76.191.135:agrisense-data/site-videos/
