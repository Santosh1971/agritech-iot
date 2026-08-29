# WPC LoRa Hardware Test Tool

A quick way to check whether a Master or Pump Node board's LoRa radio hardware is actually working — independent of the real WPC firmware/protocol. Use this whenever a board seems unresponsive and you're not sure if it's a real hardware fault (bad antenna, bad solder joint, bad LoRa module) or something in the WPC firmware logic itself.

Location: `firmware/lora_ping_pong/`

## The three roles

Every board runs the exact same code — which role it plays is chosen at flash time, no source editing needed.

- **`ping`** — sends a packet, waits for a reply. If no reply arrives within 5 seconds, it automatically retries the same packet (so one dropped packet, which is normal on any real RF link, doesn't make the test look falsely "stuck").
- **`pong`** — waits for a packet, replies to it.
- **`idle`** — does **not** touch the radio at all. Runs a quick LED self-test, then sits with the LED solid on and does nothing else for the rest of its life. Safe to leave plugged in alongside boards you're actively testing — it can never interfere with the test since it never transmits or receives anything.

Every role prints the board's own unique ID (`[ID] Board ID: 0x...`, derived from its MAC address) right at boot, so you always know exactly which physical board a given terminal window belongs to.

## How to run a test

**1. Find what's plugged in:**
```bash
ls /dev/cu.usbserial-*
```

**2. Pick two boards to test against each other**, and flash the rest (if any are plugged in at the same time) as `idle` so they can't interfere:

```bash
cd ~/Projects/agritech-iot/products/WPC-pumpcontroller/firmware/lora_ping_pong
pio run -e ping -t upload --upload-port /dev/cu.usbserial-XXXX
pio run -e pong -t upload --upload-port /dev/cu.usbserial-YYYY
pio run -e idle -t upload --upload-port /dev/cu.usbserial-ZZZZ
```
(repeat the `idle` line for each other board you have plugged in but aren't testing)

**3. Open two monitor windows**, one for each board under test:
```bash
pio device monitor -p /dev/cu.usbserial-XXXX -b 115200
```
```bash
pio device monitor -p /dev/cu.usbserial-YYYY -b 115200
```

## Reading the result

**Good (hardware is fine):**
```
[LoRa] TX done: PING:12
[LoRa] RX: "PONG:13"  RSSI: -58.00 dBm  SNR: 10.25 dB
[LoRa] TX done: PING:13
```
Continuous exchange, consistent RSSI/SNR values, counter climbing steadily on both sides.

**Bad (real problem, worth investigating):**
```
[LoRa] TX done: PING:0
[LoRa] No reply -- retrying same packet
[LoRa] No reply -- retrying same packet
[LoRa] No reply -- retrying same packet
```
Repeating retries with zero successful exchange, sustained over a minute or more. If you see this:
1. Check the antenna connection on **both** boards — reseat firmly. This has been the actual cause every time we've hit this on real hardware so far.
2. If reseating doesn't help, try swapping antennas between the two boards — if the failure follows the antenna, that confirms it.
3. If it still fails with a known-good antenna, the fault is likely on the board itself (LoRa module seating, solder joints) — worth a closer physical inspection or swapping in a different board to isolate which one is actually at fault.

One dropped packet here and there, or an occasional single retry, is normal — LoRa doesn't guarantee every packet gets through even on healthy hardware. It's *sustained*, repeated failure that indicates a real fault.

## Important — this is a diagnostic tool, not a deployment

Boards flashed with `ping`/`pong`/`idle` are **not** running the real WPC Master or Pump Node firmware. Once you've confirmed the hardware is healthy, reflash the board back to its actual role:

```bash
cd ~/Projects/agritech-iot/products/WPC-pumpcontroller/firmware/master_node
pio run -t upload --upload-port /dev/cu.usbserial-XXXX
```
or
```bash
cd ~/Projects/agritech-iot/products/WPC-pumpcontroller/firmware/pump_node
pio run -t upload --upload-port /dev/cu.usbserial-XXXX
```
