#!/usr/bin/env python3
"""Build the static workshop labs page (site/) from labs/*.

Output is plain static files, served by the agrisense web app at agrisenseandcontrol.in/workshop:
  site/index.html          - the page (code inlined, works from any static host)
  site/labs/<lab>/...      - sketch.ino, diagram.json, libraries.txt for download
and the same files are copied to webapp/agrisense-webapp/public/workshop/.

Re-run after pasting Wokwi project links into wokwi-links.json.
"""
import html
import json
import shutil
from pathlib import Path

HERE = Path(__file__).parent
LABS = HERE / "labs"
SITE = HERE / "site"
# Served by the agrisense web app at https://agrisenseandcontrol.in/workshop
BASE = "/workshop/"
WEBAPP_PUBLIC = HERE.parents[2] / "webapp" / "agrisense-webapp" / "public" / "workshop"

links = {k: v for k, v in json.loads((HERE / "wokwi-links.json").read_text()).items() if not k.startswith("_")}

LAB_INFO = [
    {
        "id": "lab1-dht22", "no": "Lab 1", "title": "Temperature & humidity (DHT22)",
        "learn": "Upload your first program, blink an LED and read a real sensor.",
        "wiring": ["DHT22 VCC → 3V3", "DHT22 DATA → GPIO 4", "DHT22 GND → GND", "LED + 220 Ω → GPIO 2 (DevKit V1 has one on the board)"],
        "try": "Click the DHT22 while the simulation runs and move the temperature slider.",
        "practicals": "Practicals 4, 7, 12",
    },
    {
        "id": "lab2a-flow", "no": "Lab 2A", "title": "Water flow meter (YF-S201)",
        "learn": "Count sensor pulses with an interrupt and turn them into L/min and litres.",
        "wiring": ["Red → 5V (VIN), Black → GND", "Yellow → 10 kΩ / 20 kΩ divider → GPIO 27 (5 V pulses, 3.3 V pin)", "Wokwi: the knob on GPIO 34 is the tap; GPIO 25 makes the pulses"],
        "try": "Turn the tap knob and watch L/min change. On a real board set SIMULATE_FLOW to 0.",
        "practicals": "Practicals 6, 10",
    },
    {
        "id": "lab2b-autopump", "no": "Lab 2B", "title": "Soil moisture + tank float + automatic pump",
        "learn": "Make decisions: water when dry, stop when wet, never run the pump with an empty tank.",
        "wiring": ["Soil sensor AOUT → GPIO 34 (Wokwi: knob)", "Float switch → GPIO 14 and GND (Wokwi: slide switch)", "Relay IN → GPIO 26, VCC → 5V, GND → GND", "Pump LED + 220 Ω → GPIO 26"],
        "try": "Turn the knob toward dry until the pump LED lights, then slide the float to EMPTY.",
        "practicals": "Practicals 6, 13",
    },
    {
        "id": "lab3-thingspeak", "no": "Lab 3", "title": "Send readings to the cloud (ThingSpeak)",
        "learn": "Connect to WiFi, call a web API and see live charts on your phone.",
        "wiring": ["Same as Lab 1 + soil sensor on GPIO 34", "Wokwi WiFi: Wokwi-GUEST, no password", "Real board: workshop router, 2.4 GHz, no login page"],
        "try": "Paste your ThingSpeak Write API Key, run it and open your channel on your phone.",
        "practicals": "Practicals 10, 11 · Unit 4",
    },
]

LAB4_PROMPT = """I am an agriculture student using an ESP32 DevKit V1 (30-pin) with Arduino IDE 2 and the Espressif ESP32 board package.

Connected:
- DHT22 temperature/humidity sensor on GPIO 4
- Capacitive soil moisture sensor on GPIO 34 (raw ~3000 dry, ~1300 wet)
- Relay module for a small pump on GPIO 26 (active HIGH)

Write one Arduino sketch that:
1. Starts a WiFi access point named "FarmNode-01" with password "farm12345"
2. Serves a mobile-friendly web page at 192.168.4.1 showing temperature, humidity and soil moisture %, refreshing every 2 seconds without reloading the page
3. Has a big Pump ON / Pump OFF button that switches the relay and shows the current state
4. Uses only the built-in WebServer library, plus "DHT sensor library for ESPx" for the DHT22

Then give me the wiring as a list, and explain the code section by section in simple language."""


def read(lab, name):
    p = LABS / lab / name
    return p.read_text() if p.exists() else ""


def code_block(label, text, fid):
    return (f'<div class="code"><div class="cap"><span>{label}</span>'
            f'<button class="copy" type="button" data-target="{fid}">Copy</button></div>'
            f'<pre id="{fid}">{html.escape(text)}</pre></div>')


def lab_section(info):
    lid = info["id"]
    sketch, diagram, libs = read(lid, "sketch.ino"), read(lid, "diagram.json"), read(lid, "libraries.txt").strip()
    url = links.get(lid, "").strip()
    if url:
        open_btn = f'<a class="btn primary" href="{html.escape(url)}" target="_blank" rel="noopener">Open in Wokwi</a>'
        steps = "<li>Click <b>Open in Wokwi</b>, then the green ▶ button. The first run waits 20–40 s in Wokwi’s free build queue.</li>"
    else:
        open_btn = '<a class="btn primary" href="https://wokwi.com/projects/new/esp32" target="_blank" rel="noopener">New ESP32 project in Wokwi</a>'
        steps = ("<li>Open a new ESP32 project in Wokwi.</li>"
                 "<li>Copy <b>sketch.ino</b> below and paste it over the code in the <code>sketch.ino</code> tab.</li>"
                 "<li>Copy <b>diagram.json</b> and paste it over the <code>diagram.json</code> tab.</li>")
    if libs:
        steps += f"<li>Library Manager tab → <b>+</b> → add <b>{html.escape(libs)}</b>.</li>"
    if not url:
        steps += "<li>Press the green ▶ button. The first run waits 20–40 s in Wokwi’s free build queue.</li>"
    wiring = "".join(f"<li>{html.escape(w)}</li>" for w in info["wiring"])
    return f'''
<section class="lab" id="{lid}">
  <div class="lab-head"><span class="labno">{info["no"]}</span><h2>{html.escape(info["title"])}</h2><span class="prac">{info["practicals"]}</span></div>
  <p class="learn">{html.escape(info["learn"])}</p>
  <div class="cols">
    <div class="box"><h3>Run it in Wokwi</h3><ol>{steps}</ol><div class="btns">{open_btn}<a class="btn" href="{BASE}labs/{lid}/sketch.ino" download>sketch.ino</a><a class="btn" href="{BASE}labs/{lid}/diagram.json" download>diagram.json</a></div></div>
    <div class="box"><h3>Wiring on a real board</h3><ul>{wiring}</ul><p class="try"><b>Try:</b> {html.escape(info["try"])}</p></div>
  </div>
  <details><summary>Show the code</summary>
  {code_block("sketch.ino", sketch, lid + "-ino")}
  {code_block("diagram.json (Wokwi circuit)", diagram, lid + "-dia")}
  </details>
</section>'''


def build():
    if SITE.exists():
        shutil.rmtree(SITE)
    for lab in [i["id"] for i in LAB_INFO] + ["lab4-dashboard"]:
        dst = SITE / "labs" / lab
        dst.mkdir(parents=True, exist_ok=True)
        for f in (LABS / lab).iterdir():
            shutil.copy(f, dst / f.name)

    labs_html = "".join(lab_section(i) for i in LAB_INFO)
    lab4_ref = read("lab4-dashboard", "sketch.ino")
    page = (HERE / "site_template.html").read_text()
    page = page.replace("{{LABS}}", labs_html)
    page = page.replace("{{LAB4_PROMPT}}", html.escape(LAB4_PROMPT))
    page = page.replace("{{LAB4_REF}}", code_block("lab4 reference solution (real board only)", lab4_ref, "lab4-ref"))
    (SITE / "index.html").write_text(page)
    if WEBAPP_PUBLIC.parent.parent.exists():
        if WEBAPP_PUBLIC.exists():
            shutil.rmtree(WEBAPP_PUBLIC)
        shutil.copytree(SITE, WEBAPP_PUBLIC)
        print(f"Copied to {WEBAPP_PUBLIC}")
    set_count = sum(1 for v in links.values() if v.strip())
    print(f"Built {SITE/'index.html'}  ({set_count}/{len(LAB_INFO)} Wokwi links set)")


if __name__ == "__main__":
    build()
