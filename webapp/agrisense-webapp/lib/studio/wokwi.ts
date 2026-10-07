// Stage 4: a Wokwi simulation of the student's design. Wokwi has no ESP32-S3
// kit board, so it uses the ESP32-S3 DevKit, which is also our stand-in, with
// the same GPIOs as the Mini pin map. Parts Wokwi lacks are stood in for:
// a slider for soil moisture, slide switches for the float and rain sensor,
// a push button for the flow sensor's pulses. I²C sensors other than the OLED
// aren't simulated and report a fixed value. Pin names come from
// github.com/wokwi/wokwi-boards (esp32-s3-devkitc-1) and the Wokwi part docs.
import { BLOCK_BY_ID } from "./blocks";
import { KITS, type KitKey } from "./kits";
import pinmap from "./pinmap.json";
import type { Rule } from "./automation";
import type { Ports } from "./rules";

type Part = { type: string; id: string; top: number; left: number; rotate?: number; attrs: Record<string, string> };
type Wire = [string, string, string, string[]];

function gpioFor(kit: KitKey, port: string): number | null {
  const board = (pinmap.boards as unknown as Record<string, { pins: Record<string, string> }>)[kit === "MEGA" ? "mega" : "mini"];
  const sig = port.startsWith("I2C") ? "I2C_EXT_SDA" : port;
  const hit = Object.entries(board.pins).find(([, s]) => s === sig);
  return hit ? Number(hit[0]) : null;
}

export type WokwiProject = {
  diagram: string;
  sketch: string;
  libraries: string;
  notes: string[]; // what is stood in for or not simulated, in plain words
};

export function wokwiProject(kit: KitKey, name: string, ports: Ports, rules: Rule[]): WokwiProject {
  const parts: Part[] = [{ type: "board-esp32-s3-devkitc-1", id: "esp", top: 0, left: 0, attrs: {} }];
  const wires: Wire[] = [["esp:TX", "$serialMonitor:RX", "", []], ["esp:RX", "$serialMonitor:TX", "", []]];
  const notes: string[] = [];
  const used = Object.entries(ports).filter(([, b]) => b) as [string, string][];
  let row = 0;
  const place = (p: Omit<Part, "top" | "left">) => {
    const left = 260 + (row % 3) * 150;
    const top = -120 + Math.floor(row / 3) * 170;
    row++;
    parts.push({ ...p, top, left });
  };
  let oled = false;

  for (const [port, block] of used) {
    const g = gpioFor(kit, port);
    const id = port.toLowerCase().replace(/[^a-z0-9]/g, "");
    if (block === "soil") {
      place({ type: "wokwi-slide-potentiometer", id, attrs: { travelLength: "30" } });
      wires.push([`${id}:SIG`, `esp:${g}`, "green", []], [`${id}:VCC`, "esp:3V3.1", "red", []], [`${id}:GND`, "esp:GND.1", "black", []]);
      notes.push(`Soil moisture on ${port} is a slider: slide it to change the moisture.`);
    } else if (block === "soilt") {
      place({ type: "wokwi-ds18b20", id, attrs: {} });
      wires.push([`${id}:DQ`, `esp:${g}`, "green", []], [`${id}:VCC`, "esp:3V3.1", "red", []], [`${id}:GND`, "esp:GND.1", "black", []]);
    } else if (block === "dht") {
      place({ type: "wokwi-dht22", id, attrs: {} });
      wires.push([`${id}:SDA`, `esp:${g}`, "green", []], [`${id}:VCC`, "esp:3V3.1", "red", []], [`${id}:GND`, "esp:GND.1", "black", []]);
      notes.push(`Click the DHT22 on ${port} to change the temperature and humidity.`);
    } else if (block === "float" || block === "rain") {
      place({ type: "wokwi-slide-switch", id, attrs: {} });
      wires.push([`${id}:2`, `esp:${g}`, "green", []], [`${id}:1`, "esp:GND.2", "black", []]);
      notes.push(block === "float" ? `The tank float on ${port} is a slide switch: left = water present.` : `The rain sensor on ${port} is a slide switch: left = raining.`);
    } else if (block === "flow") {
      place({ type: "wokwi-pushbutton", id, attrs: { color: "blue" } });
      wires.push([`${id}:1.l`, `esp:${g}`, "green", []], [`${id}:2.l`, "esp:GND.2", "black", []]);
      notes.push(`The flow sensor on ${port} is a button: each press is one pulse.`);
    } else if (block === "oled") {
      oled = true;
      place({ type: "board-ssd1306", id, attrs: {} });
      wires.push([`${id}:SDA`, "esp:14", "green", []], [`${id}:SCL`, "esp:15", "blue", []], [`${id}:VCC`, "esp:3V3.2", "red", []], [`${id}:GND`, "esp:GND.3", "black", []]);
    } else if (BLOCK_BY_ID[block]?.kind === "OUT") {
      place({ type: "wokwi-relay-module", id, attrs: {} });
      wires.push([`${id}:IN`, `esp:${g}`, "orange", []], [`${id}:VCC`, "esp:5V", "red", []], [`${id}:GND`, "esp:GND.4", "black", []]);
      const led = `${id}led`;
      place({ type: "wokwi-led", id: led, attrs: { color: "red", label: BLOCK_BY_ID[block].name.split(" (")[0] } });
      wires.push([`${led}:A`, `esp:${g}`, "orange", []], [`${led}:C`, "esp:GND.4", "black", []]);
    } else if (block === "bme" || block === "light") {
      notes.push(`${BLOCK_BY_ID[block].name} on ${port} isn't in Wokwi, so it reports a fixed value in the simulation.`);
    } else {
      notes.push(`${BLOCK_BY_ID[block]?.name ?? block} on ${port} isn't simulated.`);
    }
  }

  const diagram = JSON.stringify(
    { version: 1, author: "ASC Product Studio", editor: "wokwi", parts, connections: wires, dependencies: {} },
    null,
    2,
  );

  const libs: string[] = [];
  if (used.some(([, b]) => b === "soilt")) libs.push("OneWire", "DallasTemperature");
  if (used.some(([, b]) => b === "dht")) libs.push("DHT sensor library for ESPx");
  if (oled) libs.push("Adafruit SSD1306", "Adafruit GFX Library");

  return { diagram, sketch: sketch(kit, name, used, rules, oled), libraries: libs.join("\n") + "\n", notes };
}

function sketch(kit: KitKey, name: string, used: [string, string][], rules: Rule[], oled: boolean): string {
  const g = (port: string) => gpioFor(kit, port);
  const v = (port: string) => port.replace(/[^A-Za-z0-9]/g, "_");
  const has = (b: string) => used.some(([, x]) => x === b);
  const L: string[] = [];
  const push = (...s: string[]) => L.push(...s);

  push(
    `// ${name}: simulation of your ASC ${KITS[kit].name} kit design.`,
    "// Generated by the ASC Product Studio. The real board runs the studio's",
    "// firmware with the same rules; this sketch exists so you can try them here.",
    "#include <Arduino.h>",
  );
  if (has("soilt")) push("#include <OneWire.h>", "#include <DallasTemperature.h>");
  if (has("dht")) push("#include <DHTesp.h>");
  if (oled) push("#include <Wire.h>", "#include <Adafruit_SSD1306.h>");
  push("");

  for (const [port, b] of used) {
    if (b === "soilt") push(`OneWire ow_${v(port)}(${g(port)});`, `DallasTemperature temp_${v(port)}(&ow_${v(port)});`);
    if (b === "dht") push(`DHTesp dht_${v(port)};`);
    if (b === "flow") push(`volatile uint32_t pulses_${v(port)} = 0;`, `void IRAM_ATTR onPulse_${v(port)}() { pulses_${v(port)}++; }`);
  }
  if (oled) push("Adafruit_SSD1306 oled(128, 64, &Wire, -1);");
  push("");
  push("// Each reading of your design, NAN when a sensor fails (outputs then switch off).");
  const vars: string[] = [];
  for (const [port, b] of used) {
    if (["soil", "soilt", "flow", "float", "rain", "light"].includes(b)) vars.push(v(port));
    if (b === "dht" || b === "bme") vars.push(`${v(port)}_t`, `${v(port)}_h`);
    if (b === "bme") vars.push(`${v(port)}_p`);
  }
  if (vars.length) push(`float ${vars.map((x) => `${x} = NAN`).join(", ")};`);
  const outs = used.filter(([, b]) => BLOCK_BY_ID[b]?.kind === "OUT");
  if (outs.length) push(`bool ${outs.map(([p]) => `on_${v(p)} = false`).join(", ")};`);
  push(
    "",
    "// The simulation clock starts at 07:00 and runs 60 times faster, so a",
    "// whole day passes in 24 minutes and you can watch time windows work.",
    "int minuteOfDay() { return (7 * 60 + millis() / 1000) % (24 * 60); }",
    "",
    "void setup() {",
    "  Serial.begin(115200);",
  );
  for (const [port, b] of used) {
    const pin = g(port);
    if (b === "soilt") push(`  temp_${v(port)}.begin();`);
    if (b === "dht") push(`  dht_${v(port)}.setup(${pin}, DHTesp::DHT22);`);
    if (b === "float" || b === "rain") push(`  pinMode(${pin}, INPUT_PULLUP);`);
    if (b === "flow") push(`  pinMode(${pin}, INPUT_PULLUP);`, `  attachInterrupt(${pin}, onPulse_${v(port)}, FALLING);`);
    if (BLOCK_BY_ID[b]?.kind === "OUT") push(`  pinMode(${pin}, OUTPUT);`, `  digitalWrite(${pin}, LOW);`);
  }
  if (oled) push("  Wire.begin(14, 15);", "  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);");
  push("}", "", "void readSensors() {");
  for (const [port, b] of used) {
    const pin = g(port);
    if (b === "soil") push(`  ${v(port)} = analogRead(${pin}) * 100.0 / 4095;  // slider position as moisture %`);
    if (b === "soilt") push(`  temp_${v(port)}.requestTemperatures();`, `  ${v(port)} = temp_${v(port)}.getTempCByIndex(0);`, `  if (${v(port)} == DEVICE_DISCONNECTED_C) ${v(port)} = NAN;`);
    if (b === "dht") push(`  { TempAndHumidity r = dht_${v(port)}.getTempAndHumidity(); ${v(port)}_t = r.temperature; ${v(port)}_h = r.humidity; }`);
    if (b === "float" || b === "rain") push(`  ${v(port)} = digitalRead(${pin}) == LOW ? 1 : 0;`);
    if (b === "flow") push(`  ${v(port)} = pulses_${v(port)} / 7.5;  // pulses in the last second -> L/min`, `  pulses_${v(port)} = 0;`);
    if (b === "bme") push(`  ${v(port)}_t = 30.0; ${v(port)}_h = 60.0; ${v(port)}_p = 1008.0;  // not simulated: fixed values`);
    if (b === "light") push(`  ${v(port)} = 20000.0;  // not simulated: fixed value`);
  }
  push("}", "");

  push("void runRules() {", "  int now = minuteOfDay();");
  if (!rules.length) push("  // No rules yet: add them in the Build stage.");
  for (const r of rules) {
    const out = v(r.out);
    const [p, key] = r.sensor.split(":");
    const val = key ? `${v(p)}_${key}` : v(p);
    const cond: string[] = [];
    if (r.from && r.to) {
      const [fh, fm] = r.from.split(":").map(Number), [th, tm] = r.to.split(":").map(Number);
      const f = fh * 60 + fm, t = th * 60 + tm;
      cond.push(f <= t ? `now >= ${f} && now < ${t}` : `(now >= ${f} || now < ${t})`);
    }
    if (r.guard) {
      const gb = used.find(([pp]) => pp === r.guard)?.[1];
      cond.push(gb === "rain" ? `${v(r.guard)} == 0` : `${v(r.guard)} == 1`);
    }
    const level = r.when === "below"
      ? `(on_${out} ? ${val} < ${r.off} : ${val} < ${r.on})`
      : `(on_${out} ? ${val} > ${r.off} : ${val} > ${r.on})`;
    push(`  // ${r.out}: ${r.when === "below" ? `ON below ${r.on}, OFF at ${r.off}` : `ON above ${r.on}, OFF at ${r.off}`}${r.from ? `, ${r.from}-${r.to}` : ""}${r.guard ? `, needs ${r.guard}` : ""}`);
    push(`  on_${out} = !isnan(${val})${cond.map((c) => ` && ${c}`).join("")} && ${level};`);
    push(`  digitalWrite(${g(r.out)}, on_${out} ? HIGH : LOW);`);
  }
  push("}", "", "void loop() {", "  readSensors();", "  runRules();", '  Serial.printf("%02d:%02d", minuteOfDay() / 60, minuteOfDay() % 60);');
  for (const x of vars) push(`  Serial.printf("  ${x}=%.1f", ${x});`);
  for (const [p] of outs) push(`  Serial.printf("  ${p}=%s", on_${v(p)} ? "ON" : "off");`);
  push('  Serial.println();');
  if (oled) {
    push("  oled.clearDisplay();", "  oled.setTextColor(SSD1306_WHITE);", "  oled.setCursor(0, 0);", `  oled.println("${name.replace(/"/g, "").slice(0, 20)}");`);
    for (const x of vars.slice(0, 4)) push(`  oled.printf("${x} %.1f\\n", ${x});`);
    push("  oled.display();");
  }
  push("  delay(1000);", "}", "");
  return L.join("\n");
}
