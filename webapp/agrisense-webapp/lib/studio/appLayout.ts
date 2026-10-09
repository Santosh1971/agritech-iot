// Stage 7: the student's phone screen, as data. The ASC Studio app (Flutter,
// products/ASC-StudentKit/mobile-app) reads this from the board over
// Bluetooth (`get_design`) and builds its screen from it; no app code is
// written per student (APP-01).
import { BLOCK_BY_ID } from "./blocks";
import { outputs, readings } from "./automation";
import type { Check, Ports } from "./rules";

export type TileKind = "value" | "graph" | "gauge" | "switch" | "lamp";
export type Tile = { ref: string; kind: TileKind; label: string; labelHi: string };
export type Alert = { ref: string; when: "below" | "above"; value: number; say: string };
export type AppLayout = { name: string; lang: "en" | "hi"; tiles: Tile[]; alerts: Alert[] };

const HINDI_SUFFIX: Record<string, string> = { t: "तापमान", h: "नमी", p: "दबाव" };

// Everything a tile can show: each reading, each output, and the float/rain states.
export function tileSources(ports: Ports): { ref: string; label: string; labelHi: string; unit: string; output: boolean }[] {
  const out: { ref: string; label: string; labelHi: string; unit: string; output: boolean }[] = [];
  for (const r of readings(ports)) {
    const [port, key] = r.ref.split(":");
    const b = BLOCK_BY_ID[ports[port] ?? ""];
    const hi = key ? `${b?.hindi.split(" ")[0] ?? ""} ${HINDI_SUFFIX[key] ?? ""}`.trim() : b?.hindi ?? r.label;
    out.push({ ref: r.ref, label: r.label.replace(/ \(.*\)$/, ""), labelHi: hi, unit: r.unit, output: false });
  }
  for (const [port, id] of Object.entries(ports)) {
    if (id === "float" || id === "rain") {
      const b = BLOCK_BY_ID[id];
      out.push({ ref: port, label: id === "float" ? "Tank" : "Rain", labelHi: b.hindi, unit: "", output: false });
    }
  }
  for (const o of outputs(ports)) {
    const b = BLOCK_BY_ID[ports[o.port]!];
    out.push({ ref: o.port, label: b.name.replace(/ \(.*\)$/, ""), labelHi: b.hindi, unit: "", output: true });
  }
  return out;
}

export function defaultLayout(ports: Ports, title: string): AppLayout {
  const tiles: Tile[] = tileSources(ports).map((s, i) => ({
    ref: s.ref,
    kind: s.output ? "switch" : ["float", "rain", "pir", "door"].includes(ports[s.ref] ?? "") ? "lamp" : i === 0 ? "graph" : "value",
    label: s.label,
    labelHi: s.labelHi,
  }));
  const alerts: Alert[] = [];
  for (const [port, id] of Object.entries(ports)) {
    if (id === "float") alerts.push({ ref: port, when: "below", value: 0.5, say: "Tank is empty" });
    if (id === "door") alerts.push({ ref: port, when: "above", value: 0.5, say: "Door is open" });
    if (id === "pir") alerts.push({ ref: port, when: "above", value: 0.5, say: "Movement in the field" });
  }
  return { name: shortName(title), lang: "en", tiles, alerts };
}

// A phone title bar fits about 24 characters: cut at a word, never mid-word.
function shortName(title: string): string {
  const t = title.split(":")[0].trim();
  if (t.length <= 24) return t;
  const cut = t.slice(0, 25);
  return cut.slice(0, cut.lastIndexOf(" ") > 8 ? cut.lastIndexOf(" ") : 24).trim();
}

export function checkLayout(ports: Ports, l: AppLayout): Check[] {
  const out: Check[] = [];
  const refs = new Set(tileSources(ports).map((s) => s.ref));
  if (!l.name.trim()) out.push({ level: "bad", text: "Give your app a name." });
  if (!l.tiles.length) out.push({ level: "bad", text: "Add at least one tile." });
  for (const t of l.tiles) {
    if (!refs.has(t.ref)) out.push({ level: "bad", text: `A tile shows ${t.ref}, which isn't in your design any more. Remove it.` });
    if (!t.label.trim()) out.push({ level: "bad", text: `The tile for ${t.ref} needs a label.` });
  }
  for (const a of l.alerts) {
    if (!refs.has(a.ref)) out.push({ level: "bad", text: `An alert watches ${a.ref}, which isn't in your design any more.` });
    if (!a.say.trim()) out.push({ level: "bad", text: `The alert on ${a.ref} needs a message.` });
  }
  const outs = outputs(ports).map((o) => o.port);
  const missing = outs.filter((p) => !l.tiles.some((t) => t.ref === p));
  if (missing.length) out.push({ level: "warn", text: `No switch for ${missing.join(", ")}: you won't be able to switch ${missing.length > 1 ? "them" : "it"} from the phone.` });
  if (!out.some((c) => c.level === "bad")) out.push({ level: "ok", text: "The layout matches your design." });
  return out;
}

export function cleanLayout(raw: unknown, fallbackName: string): AppLayout {
  const x = (raw ?? {}) as Record<string, unknown>;
  const kinds: TileKind[] = ["value", "graph", "gauge", "switch", "lamp"];
  const tiles = Array.isArray(x.tiles) ? x.tiles.slice(0, 16).map((t) => {
    const y = (t ?? {}) as Record<string, unknown>;
    return {
      ref: String(y.ref ?? "").slice(0, 12),
      kind: kinds.includes(y.kind as TileKind) ? (y.kind as TileKind) : "value",
      label: String(y.label ?? "").slice(0, 30),
      labelHi: String(y.labelHi ?? "").slice(0, 30),
    };
  }) : [];
  const alerts = Array.isArray(x.alerts) ? x.alerts.slice(0, 8).map((a) => {
    const y = (a ?? {}) as Record<string, unknown>;
    return { ref: String(y.ref ?? "").slice(0, 12), when: y.when === "above" ? "above" as const : "below" as const, value: Number(y.value) || 0, say: String(y.say ?? "").slice(0, 60) };
  }) : [];
  return { name: String(x.name ?? fallbackName).slice(0, 30), lang: x.lang === "hi" ? "hi" : "en", tiles, alerts };
}
