// The design as the board receives it: firmware/asc-studio-fw/src/design.cpp
// parses exactly this shape (see the firmware README's protocol section).
import type { Rule } from "./automation";
import { tileSources, type AppLayout } from "./appLayout";
import type { KitKey } from "./kits";
import type { Ports } from "./rules";

export type DeviceConfig = {
  kit: "mini" | "mega";
  design: number;
  name: string;
  tzOffsetMin: number;
  ports: Record<string, string>;
  rules: Rule[];
  logEveryMin: number;
  app?: AppLayout & { tiles: (AppLayout["tiles"][number] & { unit?: string })[] };
};

export function deviceConfig(kit: KitKey, version: number, name: string, ports: Ports, rules: Rule[], app?: AppLayout | null): DeviceConfig {
  return {
    kit: kit === "MEGA" ? "mega" : "mini",
    design: version,
    name: name.slice(0, 39),
    tzOffsetMin: 330, // IST
    ports: Object.fromEntries(Object.entries(ports).filter(([, b]) => b)) as Record<string, string>,
    rules,
    logEveryMin: 10,
    // Each tile carries its unit, so the phone can show "28.4 °C".
    ...(app ? { app: withUnits(app, ports) } : {}),
  };
}

function withUnits(app: AppLayout, ports: Ports) {
  const units = new Map(tileSources(ports).map((t) => [t.ref, t.unit]));
  return { ...app, tiles: app.tiles.map((t) => (units.get(t.ref) ? { ...t, unit: units.get(t.ref) } : t)) };
}
