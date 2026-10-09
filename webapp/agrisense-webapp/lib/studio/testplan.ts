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
      case "probet": out.push({ ...base, todo: "Hold the probe tightly in your hand. It should warm up by 2 °C.", ref: port, pass: (v, b0) => v - b0 >= 2 }); break;
      case "raing": out.push({ ...base, todo: "Tip the bucket by hand three times. Rain should go above 0 mm.", ref: port, pass: (v) => v > 0 }); break;
      case "pir": out.push({ ...base, todo: "Stay still for 10 seconds, then wave your hand in front of the sensor. It should see motion.", ref: port, pass: (_v, _b, seen) => seen.has(0) && seen.has(1) }); break;
      case "door": out.push({ ...base, todo: "Move the magnet away from the switch, then back. The door should read open, then closed.", ref: port, pass: (_v, _b, seen) => seen.has(0) && seen.has(1) }); break;
      case "tds": out.push({ ...base, todo: "Dip the probe in a glass of tap water. TDS should go above 30 ppm.", ref: port, pass: (v) => v > 30 }); break;
      case "level": out.push({ ...base, todo: "Point the sensor at the floor, then move it 20 cm closer. The distance should change by 10 cm or more.", ref: port, pass: (v, b0) => Math.abs(v - b0) >= 10 }); break;
      case "sht": out.push({ ...base, todo: "Breathe on the sensor for a few seconds. Humidity should rise by 5 %.", ref: `${port}:h`, pass: (v, b0) => v - b0 >= 5 }); break;
      case "co2": out.push({ ...base, todo: "Breathe on the sensor for 10 seconds. CO₂ should go above 1,000 ppm (it updates every 5 s).", ref: `${port}:c`, pass: (v) => v > 1000 }); break;
      case "irtemp": out.push({ ...base, todo: "Point the sensor at your palm from 5 cm away. It should read between 28 and 38 °C.", ref: port, pass: (v) => v >= 28 && v <= 38 }); break;
      default:
        if (BLOCK_BY_ID[b]?.kind === "OUT") out.push({ ...base, todo: `Switch ${port} on and off from here.`, output: true, confirm: "I heard the relay click and saw its LED light." });
    }
  }
  return out;
}

