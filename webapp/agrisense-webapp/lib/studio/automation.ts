// Automation rules: "turn OUT1 on when S1 drops below 30 %, off at 45 %".
// The studio writes them; firmware/asc-studio-fw/src/rules.h runs them. The
// two must agree on these semantics:
//   below: ON when value < on; once ON, stays ON until value >= off (off > on)
//   above: ON when value > on; once ON, stays ON until value <= off (off < on)
//   from/to: only inside this daily window (may cross midnight); OFF outside it
//   guard: a float or rain sensor that must say "safe to run"
// A failed sensor always means OFF.
import { BLOCK_BY_ID } from "./blocks";
import type { Check, Ports } from "./rules";

export type Rule = {
  out: string;
  sensor: string; // "S1", or "S3:t" for a block with several readings
  when: "below" | "above";
  on: number;
  off: number;
  from?: string; // "06:00"
  to?: string;
  guard?: string; // port of a float switch or rain sensor
};

export type Reading = { ref: string; label: string; unit: string; min: number; max: number; step: number };

// Every reading a design can produce, in the order of its ports.
export function readings(ports: Ports): Reading[] {
  const out: Reading[] = [];
  for (const [port, id] of Object.entries(ports)) {
    if (!id) continue;
    const at = `(${port})`;
    switch (id) {
      case "soil": out.push({ ref: port, label: `Soil moisture ${at}`, unit: "%", min: 0, max: 100, step: 1 }); break;
      case "soilt": out.push({ ref: port, label: `Soil temperature ${at}`, unit: "°C", min: -10, max: 60, step: 0.5 }); break;
      case "flow": out.push({ ref: port, label: `Water flow ${at}`, unit: "L/min", min: 0, max: 60, step: 0.5 }); break;
      case "light": out.push({ ref: port, label: `Light ${at}`, unit: "lux", min: 0, max: 100000, step: 100 }); break;
      case "probet": out.push({ ref: port, label: `Temperature ${at}`, unit: "°C", min: -40, max: 85, step: 0.5 }); break;
      case "raing": out.push({ ref: port, label: `Rain, last 24 h ${at}`, unit: "mm", min: 0, max: 300, step: 1 }); break;
      case "pir": out.push({ ref: port, label: `Motion (1 = seen) ${at}`, unit: "", min: 0, max: 1, step: 0.1 }); break;
      case "door": out.push({ ref: port, label: `Door (1 = open) ${at}`, unit: "", min: 0, max: 1, step: 0.1 }); break;
      case "tds": out.push({ ref: port, label: `Water TDS ${at}`, unit: "ppm", min: 0, max: 2000, step: 10 }); break;
      case "level": out.push({ ref: port, label: `Distance down to water ${at}`, unit: "cm", min: 3, max: 450, step: 1 }); break;
      case "irtemp": out.push({ ref: port, label: `Leaf temperature ${at}`, unit: "°C", min: -20, max: 80, step: 0.5 }); break;
      case "sht":
        out.push({ ref: `${port}:t`, label: `Air temperature ${at}`, unit: "°C", min: -10, max: 60, step: 0.5 });
        out.push({ ref: `${port}:h`, label: `Air humidity ${at}`, unit: "%", min: 0, max: 100, step: 1 });
        break;
      case "co2":
        out.push({ ref: `${port}:c`, label: `CO₂ ${at}`, unit: "ppm", min: 400, max: 5000, step: 50 });
        out.push({ ref: `${port}:t`, label: `Air temperature ${at}`, unit: "°C", min: -10, max: 60, step: 0.5 });
        out.push({ ref: `${port}:h`, label: `Air humidity ${at}`, unit: "%", min: 0, max: 100, step: 1 });
        break;
      case "dht":
        out.push({ ref: `${port}:t`, label: `Air temperature ${at}`, unit: "°C", min: -10, max: 60, step: 0.5 });
        out.push({ ref: `${port}:h`, label: `Air humidity ${at}`, unit: "%", min: 0, max: 100, step: 1 });
        break;
      case "bme":
        out.push({ ref: `${port}:t`, label: `Air temperature ${at}`, unit: "°C", min: -10, max: 60, step: 0.5 });
        out.push({ ref: `${port}:h`, label: `Air humidity ${at}`, unit: "%", min: 0, max: 100, step: 1 });
        out.push({ ref: `${port}:p`, label: `Air pressure ${at}`, unit: "hPa", min: 800, max: 1100, step: 1 });
        break;
    }
  }
  return out;
}

export function guards(ports: Ports): { port: string; label: string }[] {
  return Object.entries(ports)
    .filter(([, id]) => id === "float" || id === "rain")
    .map(([port, id]) => ({ port, label: id === "float" ? `Tank float on ${port} shows water` : `Rain sensor on ${port} reads dry` }));
}

export function outputs(ports: Ports): { port: string; label: string }[] {
  return Object.entries(ports)
    .filter(([, id]) => id && BLOCK_BY_ID[id]?.kind === "OUT")
    .map(([port, id]) => ({ port, label: `${BLOCK_BY_ID[id!].name} (${port})` }));
}

// Sensible first rules, so a student starts from something that works.
export function defaultRules(ports: Ports): Rule[] {
  const rs = readings(ports);
  const soil = rs.find((r) => r.unit === "%" && r.label.startsWith("Soil"));
  const temp = rs.find((r) => r.label.startsWith("Air temperature"));
  const co2 = rs.find((r) => r.label.startsWith("CO₂"));
  const motion = rs.find((r) => r.label.startsWith("Motion"));
  const door = rs.find((r) => r.label.startsWith("Door"));
  const g = guards(ports);
  const guard = g.find((x) => x.label.startsWith("Tank"))?.port ?? g[0]?.port;
  const rules: Rule[] = [];
  for (const [port, id] of Object.entries(ports)) {
    if ((id === "pump" || id === "valve" || id === "valve24") && soil) {
      rules.push({ out: port, sensor: soil.ref, when: "below", on: 30, off: 45, from: "06:00", to: "18:00", ...(guard ? { guard } : {}) });
    } else if (id === "fogger" && temp) {
      rules.push({ out: port, sensor: temp.ref, when: "above", on: 35, off: 33 });
    } else if (id === "fan" && (temp || co2)) {
      rules.push(temp ? { out: port, sensor: temp.ref, when: "above", on: 32, off: 30 } : { out: port, sensor: co2!.ref, when: "above", on: 1500, off: 1000 });
    } else if (id === "heater" && temp) {
      rules.push({ out: port, sensor: temp.ref, when: "below", on: 5, off: 8 });
    } else if (id === "siren" && (motion || door)) {
      rules.push(motion ? { out: port, sensor: motion.ref, when: "above", on: 0.5, off: 0.2, from: "19:00", to: "06:00" } : { out: port, sensor: door!.ref, when: "above", on: 0.5, off: 0.2 });
    }
  }
  return rules;
}

const HHMM = /^([01]\d|2[0-3]):[0-5]\d$/;

export function checkRules(ports: Ports, rules: Rule[]): Check[] {
  const out: Check[] = [];
  const rs = readings(ports);
  const outs = outputs(ports).map((o) => o.port);
  const gs = guards(ports).map((g) => g.port);
  const seen = new Set<string>();
  for (const r of rules) {
    const name = BLOCK_BY_ID[ports[r.out] ?? ""]?.name ?? r.out;
    if (!outs.includes(r.out)) { out.push({ level: "bad", text: `A rule switches ${r.out}, but there is no output on it.` }); continue; }
    if (seen.has(r.out)) out.push({ level: "bad", text: `${name} has more than one rule. Keep one rule per output.` });
    seen.add(r.out);
    const reading = rs.find((x) => x.ref === r.sensor);
    if (!reading) { out.push({ level: "bad", text: `The rule for ${name} reads ${r.sensor}, which has no sensor.` }); continue; }
    if (!Number.isFinite(r.on) || !Number.isFinite(r.off)) { out.push({ level: "bad", text: `Give the rule for ${name} both an ON and an OFF level.` }); continue; }
    if (r.when === "below" && r.off <= r.on) out.push({ level: "bad", text: `${name}: the OFF level (${r.off}) must be above the ON level (${r.on}), or it will switch on and off all the time.` });
    if (r.when === "above" && r.off >= r.on) out.push({ level: "bad", text: `${name}: the OFF level (${r.off}) must be below the ON level (${r.on}), or it will switch on and off all the time.` });
    if ((r.from && !HHMM.test(r.from)) || (r.to && !HHMM.test(r.to)) || (!!r.from !== !!r.to)) out.push({ level: "bad", text: `${name}: write the time window as two times like 06:00 and 18:00.` });
    if (r.guard && !gs.includes(r.guard)) out.push({ level: "bad", text: `${name} waits for ${r.guard}, which has no float switch or rain sensor.` });
  }
  for (const o of outputs(ports)) {
    if (!seen.has(o.port)) out.push({ level: "warn", text: `${o.label} has no rule, so it only switches by hand from the app.` });
  }
  const pump = Object.entries(ports).find(([, id]) => id === "pump");
  const pumpRule = pump && rules.find((r) => r.out === pump[0]);
  if (pumpRule && !pumpRule.guard && gs.length) out.push({ level: "warn", text: `The pump rule doesn't check ${gs.join(" or ")}. Add it as a condition so the pump can't run dry.` });
  if (!out.some((c) => c.level === "bad") && rules.length) out.push({ level: "ok", text: "The rules are complete. Every rule turns its output off if its sensor fails." });
  return out;
}

export function describeRule(ports: Ports, r: Rule): string {
  const reading = readings(ports).find((x) => x.ref === r.sensor);
  const outName = BLOCK_BY_ID[ports[r.out] ?? ""]?.name ?? r.out;
  const what = reading ? reading.label.replace(/ \(.*\)$/, "").toLowerCase() : r.sensor;
  const u = reading?.unit === "%" ? " %" : reading ? ` ${reading.unit}` : "";
  const dir = r.when === "below" ? `drops below ${r.on}${u}` : `rises above ${r.on}${u}`;
  let s = `${outName} turns ON when ${what} ${dir}, and OFF again at ${r.off}${u}`;
  if (r.from && r.to) s += `, only between ${r.from} and ${r.to}`;
  const g = guards(ports).find((x) => x.port === r.guard);
  if (g) s += `, and only while the ${g.label.charAt(0).toLowerCase()}${g.label.slice(1)}`;
  return s + ".";
}

// Keep only well-formed rules, so nothing odd is ever stored.
export function cleanRules(raw: unknown): Rule[] {
  if (!Array.isArray(raw)) return [];
  return raw.slice(0, 8).map((r) => {
    const x = (r ?? {}) as Record<string, unknown>;
    const rule: Rule = {
      out: String(x.out ?? "").slice(0, 8),
      sensor: String(x.sensor ?? "").slice(0, 12),
      when: x.when === "above" ? "above" : "below",
      on: Number(x.on),
      off: Number(x.off),
    };
    if (x.from && x.to) { rule.from = String(x.from).slice(0, 5); rule.to = String(x.to).slice(0, 5); }
    if (x.guard) rule.guard = String(x.guard).slice(0, 8);
    return rule;
  });
}
