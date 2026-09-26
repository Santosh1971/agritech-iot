# GPSIOAM Farm IoT workshop (AGR 322) — labs and website page

- `labs/` — the four student labs as Wokwi projects (`sketch.ino`, `diagram.json`, `libraries.txt`), plus `lab4-dashboard/`, the reference solution for the AI lab (real board only). All five compile for `esp32dev`. Labs 2A and 2B were also run in Wokwi.
- `site/` — the built static labs page. `build_site.py` also copies it into `webapp/agrisense-webapp/public/workshop/`, which the web app serves at https://agrisenseandcontrol.in/workshop (no login needed).
- `site_template.html` + `build_site.py` — the source of `site/`. Edit these, not `site/index.html`.
- `wokwi-links.json` — public Wokwi project links, one per lab.

## Add the Wokwi links (about 2 minutes per lab)

1. Sign in at wokwi.com. Open https://wokwi.com/projects/new/esp32.
2. Paste `labs/<lab>/sketch.ino` into the `sketch.ino` tab and `labs/<lab>/diagram.json` into the `diagram.json` tab.
3. For labs 1 and 3, go to Library Manager → + → add **DHT sensor library for ESPx**.
4. Press ▶ to check it runs, then **Save**. Copy the project URL.
5. Paste the URL into `wokwi-links.json`, then run `python3 build_site.py`.

The page then shows an **Open in Wokwi** button for each lab. Without links it tells students to paste the code into a new project, which also works.

## Notes

- Wokwi does not officially support embedding its simulator in another site, so the page links to Wokwi instead.
- The free Wokwi plan queues builds, which takes 20–40 s. With 11 students compiling at once, expect some waiting.
- Lab 4 cannot run in Wokwi: a phone can't join a simulated ESP32's WiFi without Wokwi's paid Private Gateway.
