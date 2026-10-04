"""Turn a claude.ai Slides deck (its project/ files) into a PDF, one 1920x1080 page per slide.

Usage: python3 deck2pdf.py <deck folder containing project/deck.json> <out.pdf> [blobmap.json]

blobmap.json maps "/_blob/<id>" image sources to local image paths (the deck's
uploaded images). Uses the local Google Chrome in headless mode.
"""
import json, os, re, subprocess, sys, tempfile

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

def main():
    root, out = sys.argv[1], os.path.abspath(sys.argv[2])
    blobmap = json.load(open(sys.argv[3])) if len(sys.argv) > 3 else {}
    deck = json.load(open(os.path.join(root, "project", "deck.json")))
    fonts = "".join(f'<link rel="stylesheet" href="{f["href"]}">' for f in deck.get("faces", {}).values() if f.get("href"))
    pages = []
    for sid in deck["order"]:
        html = open(os.path.join(root, "project", "slides", f"{sid}.html")).read()
        html = re.sub(r"<aside>.*?</aside>", "", html, flags=re.S)  # speaker notes
        html = re.sub(r"<x-icon[^>]*></x-icon>", "", html)
        for src, path in blobmap.items():
            html = html.replace(f'"{src}"', f'"file://{os.path.abspath(path)}"')
        pages.append(f'<div class="page">{html}</div>')
    doc = f"""<!doctype html><html><head><meta charset="utf-8">{fonts}<style>
@page {{ size: 1920px 1080px; margin: 0 }}
* {{ box-sizing: border-box }} html, body {{ margin: 0; padding: 0 }}
h1, h2, h3, p, ul, ol {{ margin: 0 }} ul, ol {{ padding-left: 1.2em }}
.page {{ width: 1920px; height: 1080px; position: relative; overflow: hidden; page-break-after: always }}
.page > section {{ position: absolute; inset: 0; width: 1920px; height: 1080px }}
table {{ width: 100%; border-collapse: collapse }}
th {{ text-align: left; font-weight: 700; padding: 14px 18px; border-bottom: 3px solid currentColor }}
td {{ padding: 14px 18px; border-bottom: 1px solid rgba(0,0,0,0.12); vertical-align: top }}
</style></head><body>{''.join(pages)}</body></html>"""
    with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False) as f:
        f.write(doc)
        tmp = f.name
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--virtual-time-budget=8000",
                    "--allow-file-access-from-files", f"--print-to-pdf={out}", f"file://{tmp}"], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(out, os.path.getsize(out), "bytes,", len(pages), "slides")

main()
