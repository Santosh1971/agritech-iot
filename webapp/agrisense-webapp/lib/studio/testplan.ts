// Stage 6: what a student does for each block, and how the board's live
// readings prove it worked. Used by the Test page and by the server, which
// only finishes the stage when every check for the current design has passed.
import { BLOCK_BY_ID } from "./blocks";

export type TestCheck = {
  key: string;
  port: string;
  title: string;
  todo: string;
  ref?: string; // live value to watch
  pass?: (v: number, base: number, seen: Set<number>) => boolean;
  output?: boolean; // switched from this page
  confirm?: string; // something only a person can see or hear
};

export function checksFor(ports: Record<string, string | null>): TestCheck[] {
  const out: TestCheck[] = [];
  for (const [port, b] of Object.entries(ports)) {
    if (!b) continue;
    const name = BLOCK_BY_ID[b]?.name ?? b;
    const base = { key: `${port}:${b}`, port, title: `${port} · ${name}` };
    switch (b) {
      case "soil": out.push({ ...base, todo: "Dip the probe in a glass of water. Moisture should go above 70 %.", ref: port, pass: (v) => v > 70 }); break;
      case "soilt": out.push({ ...base, todo: "Hold the probe tightly in your hand. It should warm up by 2 °C.", ref: port, pass: (v, b0) => v - b0 >= 2 }); break;
      case "float": out.push({ ...base, todo: "Lift the float, then let it drop. The reading should change both ways.", ref: port, pass: (_v, _b, seen) => seen.has(0) && seen.has(1) }); break;
      case "rain": out.push({ ...base, todo: "Wet the rain plate with a few drops. It should read 'raining'.", ref: port, pass: (v) => v === 1 }); break;
      case "flow": out.push({ ...base, todo: "Blow gently through the sensor. Flow should go above 0.", ref: port, pass: (v) => v > 0 }); break;
      case "dht": out.push({ ...base, todo: "Breathe on the sensor for a few seconds. Humidity should rise by 5 %.", ref: `${port}:h`, pass: (v, b0) => v - b0 >= 5 }); break;
      case "bme": out.push({ ...base, todo: "Breathe on the sensor for a few seconds. Humidity should rise by 5 %.", ref: `${port}:h`, pass: (v, b0) => v - b0 >= 5 }); break;
      case "light": out.push({ ...base, todo: "Cover the sensor with your hand. Light should drop below 50 lux.", ref: port, pass: (v) => v < 50 }); break;
      default:
        if (BLOCK_BY_ID[b]?.kind === "OUT") out.push({ ...base, todo: `Switch ${port} on and off from here.`, output: true, confirm: "I heard the relay click and saw its LED light." });
    }
  }
  return out;
}

