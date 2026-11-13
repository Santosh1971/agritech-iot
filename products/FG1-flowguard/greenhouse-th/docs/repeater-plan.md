# FG1 Greenhouse TH: repeater version (plan)

Status: built 13 Nov 2026 as 0.2.0 (choices: one hop, every node may relay, -85 dBm). Tested on two boards
(direct path, relay switch, mixing star and repeater builds); the far-node path needs a third board.
The current star firmware stays as it is.

## Why

Every sensor node now talks straight to the controller over ESP-NOW (a star). The C3 Super Mini
antenna is weak: we measured -68 to -92 dBm with the boards side by side, and greenhouse 2 at the
first site is about 45 m away. A node that can't reach the controller directly is simply offline;
if it is the control sensor, misting stops after 2 min with "sensor fault".

The repeater version lets a node that is out of range send through a nearer node.

## Two versions, one code base

| | Star (today, 0.1.x) | Repeater (new) |
|---|---|---|
| Builds | `controller_c3`, `node_c3` | `controller_c3_rpt`, `node_c3_rpt` (same code, `-DFG1_REPEATER=1`) |
| Topology | every node -> controller | node -> controller, or node -> repeater node -> controller |
| Release files | `FG1-Greenhouse-TH-*-C3-vX.Y.Z-*.bin` | `FG1-Greenhouse-TH-*-C3-vX.Y.Z-RPT-*.bin` |
| App | ASAC TH | same app (shows "via ..." when a node uses a repeater) |

Both versions get every logic fix (misting rules, margins, sensors) because they are one code base;
the repeater code is compiled in only with `FG1_REPEATER`. Version numbers stay shared
(e.g. 0.2.0 and 0.2.0-RPT).

**Mixing works**: the protocol version stays 1 and the repeater only adds new packet types, which
the star firmware already ignores. So a star node can pair with a repeater controller directly, and
a repeater controller works fine with only star nodes. A star controller can't use repeaters (it
ignores the new packets), so a site that needs repeaters needs the repeater controller.

## Design

**Hops: at most one repeater** (far node -> repeater -> controller). That covers "45 m with one board
in between" and keeps routing simple: no loops, no route tables. A node that itself goes through a
repeater never acts as a repeater.

**Every repeater-build node can repeat.** All nodes are mains powered (220 V at every node on site),
so they are always listening; no battery problem. A repeater is still a normal sensor node with its
own reading. The app can switch "Can relay for other sensors" off per node (default on).

**Finding a path** (extends today's HELLO sweep):
1. A node first tries the controller directly, as now.
2. Nodes paired directly with the controller (hop 0) answer HELLOs from unknown/lost nodes with a new
   `RELAY_OFFER` (their RSSI to the controller, how many nodes they already carry).
3. A node that gets no direct answer, or only a weak one (below about -85 dBm, setting), picks the
   relay with the best combined signal. It stays with it (hysteresis) unless that path fails.
4. Every 10 min, and whenever its path fails, a relayed node re-checks whether the controller is
   reachable directly and prefers direct when it is good.

**Sending readings**:
- New `RELAY_DATA`: the repeater wraps the far node's DATA with the origin MAC and the RSSI it heard,
  and sends it on to the controller.
- New `DATA_ACK` from the controller (sent back via the repeater for relayed nodes). Today a node
  counts a reading as delivered when the radio confirms the next hop; with a repeater that only
  proves the first hop, so relayed nodes count delivery from `DATA_ACK` instead. Direct nodes keep
  today's behaviour (no extra traffic).
- The repeater keeps one waiting reading per child and retries it twice; no big queues.

**Pairing in the app**: "New sensors found" also lists nodes heard through a repeater, marked
"via FG1N_xxxx". Add, Remove and "Flash its lights" travel the same path (`RELAY_CMD`).

**Safety stays the same**: if a repeater dies, its children stop reporting; the controller already
treats a control sensor with no fresh reading for 2 min as a sensor fault and stops misting. The app
shows which sensors depended on that repeater.

**Limits**: still 8 nodes per controller in total, at most 4 children per repeater (memory and
airtime). Channel: everyone stays on the controller's WiFi channel, as today.

## App changes (ASAC TH)

- Sensor card: "Direct" or "via FG1N_6AF8", the signal of each hop, % delivered end-to-end.
- Node settings: "Can relay for other sensors" switch (repeater build only; hidden otherwise).
- Controller info shows "Repeater firmware" so the right release files are used for updates.

## New packets (protocol version stays 1)

| Type | Direction | Contents |
|---|---|---|
| 8 `RELAY_OFFER` | repeater -> far node | controller id, repeater's RSSI to controller, children count |
| 9 `RELAY_JOIN` | far node -> repeater | "carry me" (accepted if < 4 children) |
| 10 `RELAY_UP` | repeater -> controller | origin MAC, RSSI it heard, wrapped HELLO / PAIR_OK / DATA / NODE_INFO |
| 11 `RELAY_DOWN` | controller -> repeater | origin MAC, wrapped PAIR / UNPAIR / IDENTIFY / DATA_ACK / RELAY_CFG |
| 12 `DATA_ACK` | controller -> node | origin MAC, seq (end-to-end "reading arrived") |
| 13 `RELAY_PAIR` | repeater -> far node | PAIR that also carries the controller's MAC |
| 14 `NODE_INFO` | node -> controller, every 60 s | relay on/off, children, parent, parent RSSI |
| 15 `RELAY_CFG` | controller -> node | relay on/off (from the app) |

Delivery: every reading a repeater-version controller receives gets a `DATA_ACK`. A repeater-version
node counts delivery by it once it has seen one from its controller, and always when it goes
through a repeater. It forgets that when it loses the controller, so it relearns after the
controller is reflashed with the star firmware (which never sends `DATA_ACK`).

Bench commands: node `relay on|off`, `nodirect on|off` (ignore the controller's own packets);
controller `nodirect <node id>|off` (ignore that node's direct packets). With both set, a node
behaves as if out of the controller's range. App command `node_relay {mac, on}`.

## Bench test plan

Needs a **third C3 board** (we have two): controller A, repeater node B, far node C.
- Range is simulated with a bench command on C: `nodirect` makes it ignore the controller's packets,
  so it must use B. The same for a "kill B" test without unplugging.
- Tests: C joins via B; app lists C "via B"; Add / Remove / Flash lights through B; readings and
  % delivered; misting on C as the only control sensor; unplug B -> C goes offline and misting stops
  after 2 min; plug B back -> C rejoins by itself; C moved next to A -> it goes direct again;
  star node + repeater controller; 8 nodes total.
- Then a field walk-test at the site: A at the pump, B halfway, C in greenhouse 2.

## Steps and rough effort

1. Protocol + repeater logic in node firmware, behind `FG1_REPEATER` (largest part).
2. Controller: RELAY_DATA / DATA_ACK / RELAY_CMD, "via" and per-hop signal in status JSON.
3. Host tests for path choice and failover (like the MistLogic tests).
4. App: "via", hop signals, relay switch.
5. Bench test with 3 boards, then release files (STAR and RPT sets) + READ ME.

## To decide before building

- One repeater hop enough, or allow two (more range, more complexity)?
- Every repeater-build node may relay (default on), or only nodes you pick in the app?
- Weak-signal threshold for switching to a repeater (proposed -85 dBm).
- Get a third C3 board for the bench test.
- Compare with the simpler fix: a C3 board with an external antenna may make repeaters unnecessary
  at this site; worth a range test first.
