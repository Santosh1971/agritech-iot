# ASC Studio firmware

`asc-studio-fw/` is one firmware image for every ASC Student Kit, and for the ESP32-S3 DevKit stand-in used until the kit boards exist. A student's design is never compiled in. The studio sends it as JSON over USB serial, and the firmware stores it in NVS. Bluetooth comes with the App stage.

## Build and flash

```bash
cd asc-studio-fw
pio run -e asc_s3                 # build
pio run -e asc_s3 -t upload       # flash over the DevKit's "USB" port (native USB)
pio test -e native                # rule-engine unit tests on your computer
```

`scripts/gen_board_map.py` runs before every build and writes `src/board_map.h` from `../hardware/pinmap.json`. The firmware therefore always uses the checked pin map.

CI (`.github/workflows/asc-kit-firmware.yml`) builds every push that touches this folder. Pushes to `main` upload a `dev-<sha>` build as product `ASC_KIT`. A tag `asc-v1.2.3` uploads release `1.2.3`. The studio's Build stage flashes the newest release; admins also see dev builds.

## DevKit stand-in

Use an **ESP32-S3-DevKitC-1-N8**, which has no PSRAM. On the N8R8 and N16R8 DevKits, GPIO35–37 are wired to the octal PSRAM, and those are the Mini's relay pins OUT1 and OUT2. Wire modules to the GPIOs in the Mini column of `../docs/StudentKit_Architecture_v0.1.md` §2: S1–S4 on GPIO1–4, the student I²C port on SDA 14 / SCL 15, OUT1 on GPIO35, OUT2 on GPIO36. Use the board's **USB** connector, not the UART one.

The DevKit has no BOARD_ID divider. The firmware reads that as "Mini, stand-in" and says so in its `hello` reply.

## Serial protocol

The link runs at 115200 baud over the native USB port. Each message is one JSON object per line, in both directions. The web app's `app/studio/[id]/device.ts` speaks the same protocol.

| Studio sends | Board replies |
|---|---|
| `{"cmd":"hello"}` | `{"type":"hello","fw","board","standIn","id","rtc","time","design":{…}}` |
| `{"cmd":"config","config":{…}}` | `{"type":"config","ok":true,"design":3}`, or `ok:false` with an `error` written for students |
| `{"cmd":"time","unix":…}` | `{"type":"time","ok":true}`. Sets the clock, and the RTC if one is fitted |
| `{"cmd":"live","on":true}` | then `{"type":"live","time","values":{"S1":41.2,"S3:t":33.1},"outputs":{"OUT1":0},"manual":{…}}` every second |
| `{"cmd":"out","port":"OUT1","on":true}` | Manual control. It returns to the rules after 10 minutes or after `{"cmd":"auto"}` |
| `{"cmd":"selftest"}` | `{"type":"selftest","results":[{"port","block","ok","detail"}]}` |
| `{"cmd":"beep"}` | A short beep |

The design JSON is built by `webapp/agrisense-webapp/lib/studio/deviceConfig.ts`:

```json
{"kit":"mini","design":3,"name":"Nursery Guard","tzOffsetMin":330,
 "ports":{"S1":"soil","S2":"float","S3":"dht","OUT1":"pump","OUT2":"fogger"},
 "rules":[{"out":"OUT1","sensor":"S1","when":"below","on":30,"off":45,"from":"06:00","to":"18:00","guard":"S2"},
          {"out":"OUT2","sensor":"S3:t","when":"above","on":35,"off":33}],
 "cal":{"S1":{"dry":2600,"wet":1100}}}
```

## Safety behaviour

- Every output is OFF at power-up, whenever a new design is loaded, and whenever the board rejects a design.
- A rule turns its output OFF when:
  - its sensor fails or is unplugged;
  - its guard (a float switch or rain sensor) does not say it is safe to run;
  - the time is unknown and the rule has a time window.
