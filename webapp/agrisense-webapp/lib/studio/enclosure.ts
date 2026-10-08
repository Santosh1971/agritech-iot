// Stage 8: the box. The kit ships with a stock IP65 box; the student places
// a cable gland for every cable the design uses (plus power and, on Mega, the
// antenna), and gets a 1:1 printable drilling template and an OpenSCAD model
// for a 3D-printed version. The box is the one WM1 and WPC already use
// (kit architecture §8): 180 × 130 × 100 mm outside, with a clear lid.
import { BLOCK_BY_ID } from "./blocks";
import type { KitKey } from "./kits";
import type { Check, Ports } from "./rules";

export type Box = { id: string; name: string; w: number; h: number; d: number; wall: number; measured: boolean; fits: KitKey[] };

// w × h × d are inside sizes: w along the bottom wall, h from the bottom wall
// to the top, d from the back to the lid. Outside 130 × 180 × 100 less 3 mm
// walls; the wall thickness is an estimate until a box is cut open.
export const BOXES: Box[] = [
  { id: "ASC-180", name: "ASC kit box (clear lid)", w: 124, h: 174, d: 94, wall: 3, measured: true, fits: ["MINI", "MEGA"] },
];

// Gland hole sizes (mm) and the space each needs around it for the nut.
const GLAND = {
  M12: { hole: 12.5, pitch: 20, label: "M12 gland (sensor cable)" },
  M16: { hole: 16.5, pitch: 25, label: "M16 gland (power / pump cable)" },
  SMA: { hole: 6.5, pitch: 14, label: "Antenna (SMA bulkhead)" },
} as const;
export type GlandKind = keyof typeof GLAND;

export type Hole = { id: string; kind: GlandKind; label: string; x: number; y: number; dia: number };
// `window` is kept for saved choices from before the clear lid; it is ignored.
export type EnclosureChoice = { box: string; window: boolean };
export type Plan = { box: Box; holes: Hole[]; window: { w: number; h: number } | null; checks: Check[] };

function cables(kit: KitKey, ports: Ports): { id: string; kind: GlandKind; label: string }[] {
  const out: { id: string; kind: GlandKind; label: string }[] = [];
  for (const [port, id] of Object.entries(ports)) {
    if (!id) continue;
    const b = BLOCK_BY_ID[id];
    if (!b || id === "oled" || id === "lora" || id === "gsm") continue; // inside the box, or an antenna below
    const kind: GlandKind = b.kind === "OUT" ? "M16" : "M12";
    out.push({ id: port, kind, label: `${port}: ${b.name}` });
  }
  out.push({ id: "PWR", kind: "M16", label: kit === "MEGA" ? "Power / solar" : "12 V power" });
  if (kit === "MEGA" && Object.values(ports).some((b) => b === "lora" || b === "gsm")) out.push({ id: "ANT", kind: "SMA", label: "Antenna" });
  return out;
}

// Holes go on the bottom wall (glands facing down keep rain out): one row if
// they fit, else two rows when the wall is tall enough for two nuts.
export function plan(kit: KitKey, ports: Ports, choice: EnclosureChoice): Plan {
  const box = BOXES.find((b) => b.id === choice.box) ?? BOXES[0];
  const list = cables(kit, ports);
  const checks: Check[] = [];
  const usable = box.w - 2 * 15; // keep clear of the corners and screw bosses
  const width = (cs: typeof list) => cs.reduce((n, c) => n + GLAND[c.kind].pitch, 0);
  let rows: (typeof list)[] = [list];
  if (width(list) > usable && box.d >= 60) {
    // Split where the two rows come out closest in width.
    let best = 1;
    for (let i = 1; i < list.length; i++) {
      if (Math.abs(width(list.slice(0, i)) - width(list.slice(i))) < Math.abs(width(list.slice(0, best)) - width(list.slice(best)))) best = i;
    }
    rows = [list.slice(0, best), list.slice(best)];
  }
  const holes: Hole[] = [];
  rows.forEach((row, r) => {
    const y = rows.length === 1 ? box.d / 2 : box.d * (r === 0 ? 0.3 : 0.7);
    let x = 15 + Math.max(0, (usable - width(row)) / 2);
    for (const c of row) {
      const g = GLAND[c.kind];
      holes.push({ id: c.id, kind: c.kind, label: c.label, x: Math.round((x + g.pitch / 2) * 10) / 10, y: Math.round(y * 10) / 10, dia: g.hole });
      x += g.pitch;
    }
  });
  const widest = Math.max(...rows.map(width));
  if (widest > usable) {
    checks.push({ level: "bad", text: `${list.length} glands need more wall than ${box.name} has (about ${usable} mm per row). Choose a bigger box.` });
  } else {
    checks.push({ level: "ok", text: `${list.length} glands fit on the bottom wall${rows.length > 1 ? " in two rows" : ""}, with room for each nut.` });
  }
  if (!box.fits.includes(kit)) checks.push({ level: "bad", text: `The ${kit === "MEGA" ? "Mega" : "Mini"} board doesn't fit in ${box.name}.` });
  if (Object.values(ports).includes("oled")) checks.push({ level: "ok", text: "The lid is clear, so the display and the board's LEDs show through it. No window to cut." });
  if (!box.measured) checks.push({ level: "warn", text: "This box's size hasn't been measured yet. Measure your real box before drilling." });
  return { box, holes, window: null, checks };
}

// The bottom wall at 1:1, as an SVG sized in millimetres. Printed at 100 %
// ("actual size"), it can be taped to the box and drilled through. The 50 mm
// bar is there to check the print scale with a ruler before drilling.
export function drillingSvg(p: Plan, title: string): string {
  const { box } = p;
  const m = 12;
  const W = box.w + 2 * m, H = box.d + 2 * m + 32;
  const hole = (h: Hole) => {
    const cx = m + h.x, cy = m + h.y, r = h.dia / 2;
    return `<circle cx="${cx}" cy="${cy}" r="${r}" fill="none" stroke="#000" stroke-width="0.3"/>` +
      `<line x1="${cx - r - 2}" y1="${cy}" x2="${cx + r + 2}" y2="${cy}" stroke="#000" stroke-width="0.15"/>` +
      `<line x1="${cx}" y1="${cy - r - 2}" x2="${cx}" y2="${cy + r + 2}" stroke="#000" stroke-width="0.15"/>` +
      `<text x="${cx}" y="${cy + r + 5}" font-size="3" text-anchor="middle" font-family="sans-serif">${h.id} · Ø${h.dia}</text>`;
  };
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${W}mm" height="${H}mm" viewBox="0 0 ${W} ${H}">
<rect x="${m}" y="${m}" width="${box.w}" height="${box.d}" fill="none" stroke="#000" stroke-width="0.4" stroke-dasharray="2 1"/>
<text x="${m}" y="${m - 6}" font-size="3.4" font-family="sans-serif">${escapeXml(title.length > 60 ? title.slice(0, 57) + "…" : title)}</text>
<text x="${m}" y="${m - 2}" font-size="3" font-family="sans-serif">Bottom wall of ${escapeXml(box.name)}, ${box.w} × ${box.d} mm${box.measured ? "" : " · size not yet measured"}</text>
${p.holes.map(hole).join("\n")}
<line x1="${m}" y1="${H - 14}" x2="${m + 50}" y2="${H - 14}" stroke="#000" stroke-width="0.6"/>
<line x1="${m}" y1="${H - 16}" x2="${m}" y2="${H - 12}" stroke="#000" stroke-width="0.4"/>
<line x1="${m + 50}" y1="${H - 16}" x2="${m + 50}" y2="${H - 12}" stroke="#000" stroke-width="0.4"/>
<text x="${m + 55}" y="${H - 15}" font-size="3" font-family="sans-serif">This line must measure exactly 50 mm.</text>
<text x="${m + 55}" y="${H - 11}" font-size="3" font-family="sans-serif">If it doesn't, print again at 100 % / actual size.</text>
<text x="${m}" y="${H - 5}" font-size="2.8" font-family="sans-serif">Tape the dashed outline to the wall, punch each centre, drill a 3 mm pilot hole, then widen it with a step drill.</text>
</svg>`;
}

// A printable box with the same holes, for JLC3DP or any 3D printer.
// OpenSCAD (openscad.org) turns this into an STL: File > Export > STL.
export function openscad(p: Plan, title: string): string {
  const { box } = p;
  const holes = p.holes.map((h) => `  [${h.x}, ${h.y}, ${h.dia}], // ${h.label}`).join("\n");
  return `// ${title}: ASC kit enclosure, generated by the ASC Product Studio.
// Box: ${box.name}, inside ${box.w} x ${box.h} x ${box.d} mm${box.measured ? "" : " (placeholder size, not yet measured)"}.
// Open in OpenSCAD, press F6, then File > Export > Export as STL.

inside = [${box.w}, ${box.h}, ${box.d}];  // width, length, height (mm)
wall = ${box.wall};
boss = 7;          // screw boss diameter in the corners
// Glands on the bottom wall: [x along the wall, z from the floor, hole diameter]
holes = [
${holes}
];

difference() {
  cube([inside[0] + 2*wall, inside[1] + 2*wall, inside[2] + wall]);
  translate([wall, wall, wall]) cube(inside);
  for (h = holes)
    translate([wall + h[0], wall + 0.01, wall + h[1]])
      rotate([90, 0, 0]) cylinder(d = h[2], h = wall + 1, $fn = 64);
}
// Corner bosses for the lid screws.
for (x = [wall + boss/2, wall + inside[0] - boss/2], y = [wall + boss/2, wall + inside[1] - boss/2])
  translate([x, y, wall]) difference() {
    cylinder(d = boss, h = inside[2], $fn = 32);
    cylinder(d = 2.8, h = inside[2] + 1, $fn = 16);
  }
${p.window ? `// The lid needs a ${p.window.w} x ${p.window.h} mm window for the display; cut it in the lid, not this part.\n` : ""}`;
}

function escapeXml(s: string): string {
  return s.replace(/[<>&"]/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;", '"': "&quot;" })[c]!);
}
