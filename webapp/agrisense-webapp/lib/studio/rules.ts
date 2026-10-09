// The "rules decide" half of the studio (decision D5): every check here is
// deterministic, runs the same in the browser and on the server, and explains
// itself in plain language. The AI may propose a design; only these rules
// decide whether it can be built.
import { BLOCK_BY_ID } from "./blocks";
import { KITS, portOf, type KitKey } from "./kits";

export type Ports = Record<string, string | null>; // port id -> block id

export type Check = { level: "ok" | "warn" | "bad"; text: string };

const PORT_NAMES: Record<string, string> = {
  S: "a sensor port (S1, S2…)",
  I2C: "an I²C port",
  OUT: "a relay output (OUT1, OUT2…)",
  RS485: "the RS-485 port",
  LORA: "the LoRa slot",
  GSM: "the 4G slot",
};

// Why a block can't go on a port, or "" if it can.
export function whyNot(kit: KitKey, blockId: string, portId: string): string {
  const b = BLOCK_BY_ID[blockId];
  const p = portOf(kit, portId);
  if (!b) return "Unknown block.";
  if (!p) return `The ${KITS[kit].name} kit has no port called ${portId}.`;
  if (!KITS[kit].ports.some((q) => q.kind === b.kind)) {
    return `Needs ${PORT_NAMES[b.kind]}, which only the Mega kit has.`;
  }
  if (b.kind !== p.kind) return `Goes on ${PORT_NAMES[b.kind]}, not on ${portId}.`;
  return "";
}

export function power(ports: Ports) {
  // ESP32-S3 WiFi transmit peak plus the RTC and LEDs.
  let p3 = 365;
  let p5 = 0;
  for (const id of Object.values(ports)) {
    const b = id ? BLOCK_BY_ID[id] : undefined;
    if (b) { p3 += b.mA3; p5 += b.mA5; }
  }
  // The 3.3 V regulator draws its load from 5 V (85 % efficient buck assumed).
  const total5 = Math.round((p3 * 3.3) / 5 / 0.85) + p5;
  return { p3, p5, total5 };
}

export function checks(kit: KitKey, ports: Ports): Check[] {
  const out: Check[] = [];
  const used = Object.entries(ports).filter(([, b]) => b) as [string, string][];

  if (used.length === 0) {
    return [{ level: "warn", text: "No blocks yet. Tap a port, then pick a sensor or output for it." }];
  }

  const wrong = used.map(([p, b]) => [p, b, whyNot(kit, b, p)]).filter(([, , w]) => w);
  for (const [p, b, w] of wrong) out.push({ level: "bad", text: `${BLOCK_BY_ID[b]?.name ?? b} on ${p}: ${w}` });
  if (!wrong.length) {
    out.push({ level: "ok", text: "Every block is on a port it can use. Sensor ports are ADC1 pins, so analog readings keep working with WiFi and Bluetooth on." });
  }

  // I²C ports share one bus, so two chips with the same address clash.
  const addrs = new Map<string, string>();
  for (const [p, b] of used) {
    const a = BLOCK_BY_ID[b]?.i2cAddr;
    if (!a) continue;
    if (addrs.has(a)) out.push({ level: "bad", text: `${p} and ${addrs.get(a)} both use I²C address ${a}. They share one bus, so only one of them can be fitted.` });
    else addrs.set(a, p);
  }

  const relays = used.filter(([, b]) => BLOCK_BY_ID[b]?.kind === "OUT").length;
  const pw = power(ports);
  if (relays && kit === "MINI") {
    out.push({ level: "warn", text: `This design uses ${relays} relay${relays > 1 ? "s" : ""}, about ${pw.total5} mA at peak. A laptop USB port gives only 500 mA. Power the kit from the 12 V adapter or a USB charger of at least 1 A.` });
  }
  if (relays && kit === "MEGA") {
    out.push({ level: "ok", text: "Mega's relays are latching: they use power only while switching, so holding a pump ON does not drain the battery." });
  }

  const has = (id: string) => used.some(([, b]) => b === id);
  if (has("pump") && has("float")) out.push({ level: "ok", text: "The pump rule can check the tank float switch first, so the pump never runs dry." });
  if (has("pump") && !has("float") && !has("flow")) out.push({ level: "warn", text: "Nothing tells the board whether water is actually there. Add a tank float switch or a flow sensor so the pump can't run dry." });
  if (has("flow")) out.push({ level: "ok", text: "The flow sensor takes 5 V from its port. Its 5 V pulses are safe on the protected signal pin." });
  const levels = used.filter(([, b]) => b === "level").length;
  if (levels > 1) out.push({ level: "bad", text: `${levels} tank level sensors: only one fits, because it uses the board's one spare serial port.` });
  if (has("co2")) out.push({ level: "ok", text: "The CO₂ sensor draws up to 175 mA for a moment every 5 seconds. The board's 3.3 V supply has room for it." });
  const mains = used.filter(([, b]) => ["pump", "fan", "growlite", "heater"].includes(b)).length;
  if (mains) out.push({ level: "ok", text: "Pumps, exhaust fans, grow lights and heaters run on 230 V. The relay only signals the certified contactor box, so mains never reaches the board." });
  if (has("siren") && !has("pir") && !has("door") && !has("float")) out.push({ level: "warn", text: "Nothing in this design tells the siren when to sound. Add a motion sensor, a door switch or a tank float." });

  const free = KITS[kit].ports.filter((p) => !ports[p.id] && (p.kind === "S" || p.kind === "I2C" || p.kind === "OUT")).map((p) => p.id);
  if (free.length) out.push({ level: "ok", text: `Free for later: ${free.join(", ")}.` });
  return out;
}

export function canBuild(kit: KitKey, ports: Ports): boolean {
  const c = checks(kit, ports);
  return !c.some((x) => x.level === "bad") && Object.values(ports).some(Boolean);
}

// Keep only known ports and known blocks, so nothing odd is ever stored.
export function cleanPorts(kit: KitKey, raw: unknown): Ports {
  const out: Ports = {};
  if (!raw || typeof raw !== "object") return out;
  for (const p of KITS[kit].ports) {
    const v = (raw as Record<string, unknown>)[p.id];
    out[p.id] = typeof v === "string" && BLOCK_BY_ID[v] ? v : null;
  }
  return out;
}
