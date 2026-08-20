# AgriSense — Unified MQTT Topic Convention

## Why unify

Right now each product has its own ad-hoc topic scheme:
- FG1/SWC: `swc/SWC_001/<4-char-suffix>/...`
- TH Monitor: `mqtt_rx/TH/<mac>` and `mqtt_tx/TH/<mac>`
- WM1-Mini: not yet fixed ("broker address should be configurable")
- WPC: no MQTT yet (LoRa-local only, remote monitoring deferred)
- PC (GSM Pump Controller): no MQTT at all (SMS-based)

One new broker + one web app means one convention. New firmware (WM1, WPC's future cloud tier) should adopt this from the start. FG1 and TH can migrate whenever convenient — their existing topics keep working until then (the broker doesn't care what topic a device publishes to).

## Convention

```
agrisense/<product>/<deviceId>/status      (device -> cloud, retained)
agrisense/<product>/<deviceId>/data        (device -> cloud, telemetry/readings)
agrisense/<product>/<deviceId>/cmd         (cloud -> device, commands)
agrisense/<product>/<deviceId>/cmd/ack     (device -> cloud, command acknowledgement)
agrisense/<product>/<deviceId>/lwt         (device -> cloud, retained, Last Will — online/offline)
```

- `<product>` = `FG1` | `FM1` | `WM1` | `WPC` | `TH` (PC stays SMS-only, no MQTT)
- `<deviceId>` = MAC-suffixed ID already used by FG1/SWC and WPC (e.g. `FG1_A1B2`) — reuse that pattern everywhere for consistency, since it already solves the multi-device collision problem SWC hit in production
- `status` payload: small JSON, retained, so the app/dashboard can show last-known state instantly on load without waiting for a fresh publish — `{online: bool, lastSeen: ISO8601, ...product-specific fields}`
- `data` payload: telemetry — flow readings, temp/humidity, pump run state, valve state, etc. Product-specific shape, no fixed schema needed at the broker level
- `cmd` payload: `{cmdId: uuid, payloadId: string, ...params}` — the `cmdId` lets `cmd/ack` correlate acknowledgements back to the request (missing in TH's current scheme, worth adding)
- `lwt`: broker-native MQTT Last Will, set at connect time — `{"online": false}` retained, so a device that drops off the network (crash, power loss) shows offline in the app automatically without a heartbeat timeout

## Auth model

Each physical device gets its own MQTT username/password (Mosquitto ACL), scoped to only publish/subscribe on its own `agrisense/<product>/<deviceId>/#` topic tree. Prevents one compromised/misbehaving device from reading or spoofing another's topics — this is the same lesson from the earlier unauthenticated-broker incident on SWC.

The web app backend uses a separate admin-level MQTT credential with access to `agrisense/#` (all topics), so it can subscribe broadly and write incoming data to Postgres.

## WPC note

WPC's Master node is the only unit that would ever touch MQTT (Pump nodes stay LoRa-only, talking only to their Master). When WPC gets its "remote monitoring" tier later, its Master publishes to `agrisense/WPC/<masterId>/...` same as any other product — no special-casing needed.
