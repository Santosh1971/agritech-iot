// Stage 1 → stage 2: turn the student's problem into a one-page spec.
// With ANTHROPIC_API_KEY set, Claude drafts it; without one (or if the call
// fails) a fixed template drafts it from the same facts, so the stage always
// works. Either way the suggested ports go through the same rules as a
// student's own choices before anything is kept (decision D5).
import Anthropic from "@anthropic-ai/sdk";
import { BLOCKS, BLOCK_BY_ID } from "./blocks";
import { KITS, type KitKey } from "./kits";
import { cleanPorts, whyNot, type Ports } from "./rules";

import { type ProblemData, type Requirement, type SpecData } from "./problem";

export type { ProblemData, Requirement, SpecData } from "./problem";
export { problemComplete, suggestKit } from "./problem";

const SAFETY = "The board never touches 230 V. Pumps and other mains loads are switched through the certified contactor box from Agri Sensors and Controls.";

// ---------- Template (no API key, or Claude unavailable) ----------

// `design` is the board from the Architecture stage, once there is one: the
// spec then describes the parts actually chosen instead of guessing.
export function templateSpec(p: ProblemData, kit: KitKey, note?: string, design?: Ports): SpecData {
  const k = KITS[kit];
  const text = `${p.story ?? ""} ${p.crop ?? ""} ${p.water ?? ""} ${p.climate ?? ""}`.toLowerCase();
  const pick: string[] = [];
  const want = (id: string, ...words: string[]) => { if (words.some((w) => text.includes(w))) pick.push(id); };
  want("soil", "soil", "moisture", "dry");
  want("float", "tank");
  want("flow", "flow", "litre", "liter", "quantity", "volume", "how much water");
  want("dht", "humid", "hot", "temperature", "heat", "°c");
  want("light", "light", "shade", "sun");
  want("pump", "pump", "irrigat");
  want("fogger", "fog", "mist", "fan", "cool");
  want("valve", "valve", "drip");
  want("level", "tank level", "water level", "how full", "sump");
  want("probet", "cold storage", "cold room", "cold store", "water temperature", "grain");
  want("door", "door");
  want("pir", "animal", "intrusion", "wild boar", "nilgai", "cattle", "theft");
  want("siren", "animal", "intrusion", "wild boar", "nilgai", "alarm", "siren");
  want("raing", "rainfall", "how much rain");
  want("tds", "tds", "salt", "salinity", "fertigation", "hydroponic");
  want("doser", "fertigation", "dosing", "hydroponic");
  want("co2", "co2", "co₂", "carbon dioxide");
  want("irtemp", "leaf temperature", "canopy", "water stress");
  want("heater", "heater", "frost");
  want("growlite", "grow light", "artificial light");
  if (kit === "MEGA") { want("npk", "npk", "fertil", "nutrient"); if (p.network === "none") pick.push("lora"); if (p.network === "mobile") pick.push("gsm"); }
  if (!pick.length) pick.push("soil", "dht");

  const suggested = hasParts(design) ? cleanPorts(kit, design) : placeBlocks(kit, pick);
  const used = Object.entries(suggested).filter(([, b]) => b) as [string, string][];
  const sensors = used.filter(([, b]) => !BLOCK_BY_ID[b].output).map(([, b]) => BLOCK_BY_ID[b].name.toLowerCase());
  const outputs = used.filter(([, b]) => BLOCK_BY_ID[b].output).map(([, b]) => BLOCK_BY_ID[b].name.toLowerCase());
  const link = p.network === "wifi" ? "over WiFi from anywhere, and over Bluetooth at the site"
    : p.network === "mobile" ? "by SMS and 4G from anywhere, and over Bluetooth at the site"
    : "over Bluetooth at the site, with readings relayed by LoRa to a Master";

  return {
    what: `Solves this problem: ${p.story?.trim() ?? ""}`.trim(),
    madeOf: `The ${k.name} kit with ${sensors.length ? sensors.join(", ") : "its sensors"}${outputs.length ? `, switching ${outputs.join(" and ")}` : ""}.`,
    use: `A phone app shows the readings and lets you switch outputs by hand, ${link}.`,
    safety: SAFETY,
    requirements: [
      { id: "SYS-01", text: "Watch the conditions described in the problem and act on them without anyone present." },
      { id: "SYS-02", text: "Fail safe: every output goes OFF if a sensor fails or the design is invalid." },
      { id: "HW-01", text: `${k.name} kit. ${used.map(([port, b]) => `${BLOCK_BY_ID[b].name} on ${port}`).join("; ")}.` },
      { id: "SW-01", text: "Rules turn outputs on and off from the sensor readings, with a gap between the on and off levels so nothing chatters." },
      { id: "APP-01", text: "The phone shows every reading and has a switch for every output." },
      { id: "ME-01", text: "IP65 stock box with one cable gland per cable, glands facing down." },
    ],
    suggested,
    source: "template",
    note,
    generatedAt: new Date().toISOString(),
  };
}

function hasParts(design?: Ports): design is Ports {
  return !!design && Object.values(design).some(Boolean);
}

// Put each wanted block on the first free port that takes it.
function placeBlocks(kit: KitKey, ids: string[]): Ports {
  const ports = cleanPorts(kit, {});
  for (const id of ids) {
    const free = KITS[kit].ports.find((p) => !ports[p.id] && !whyNot(kit, id, p.id));
    if (free) ports[free.id] = id;
  }
  return ports;
}

// ---------- Claude ----------

const SCHEMA = {
  type: "object",
  additionalProperties: false,
  required: ["what", "made_of", "use", "requirements", "suggested_ports"],
  properties: {
    what: { type: "string", description: "What the device does and for whom, 2-4 plain sentences." },
    made_of: { type: "string", description: "Which kit and which sensors and outputs, 1-2 sentences." },
    use: { type: "string", description: "How the farmer or student uses it day to day, including the phone app, 1-3 sentences." },
    requirements: {
      type: "array",
      items: {
        type: "object",
        additionalProperties: false,
        required: ["id", "text"],
        properties: { id: { type: "string" }, text: { type: "string" } },
      },
    },
    suggested_ports: {
      type: "array",
      items: {
        type: "object",
        additionalProperties: false,
        required: ["port", "block"],
        properties: { port: { type: "string" }, block: { type: "string" } },
      },
    },
  },
} as const;

function systemPrompt(kit: KitKey): string {
  const k = KITS[kit];
  const ports = k.ports.map((p) => `${p.id} (${p.kind})`).join(", ");
  const blocks = BLOCKS.map((b) => `- ${b.id}: ${b.name}; plugs into port kind ${b.kind}`).join("\n");
  return `You draft one-page product specifications for BSc Agriculture students in India who are building a farm IoT device in the Student Product Studio run by Agri Sensors and Controls. The students are not engineers. Write in plain, short English sentences, with no jargon and no brand names other than Agri Sensors and Controls.

The student is using the ${k.name} kit: ${k.blurb}
Its ports are: ${ports}. Port kinds: S = sensor port, I2C = I²C port, OUT = low-voltage relay output, RS485, LORA, GSM.

The only sensors and outputs available are these blocks (id: name; port kind):
${blocks}

Rules:
- Suggest blocks only from the list above, by id, each on a port of the matching kind, at most one block per port. Suggest only what the problem needs.
- The board never switches 230 V. A mains pump is switched through the certified contactor box, using the "pump" block.
- If a pump is used, add a way to know water is present (float switch or flow sensor) so it cannot run dry.
- Requirements: 6 to 10 short, testable lines. Use ids SYS-01…, HW-01…, SW-01…, APP-01…, ME-01… (system, hardware, software, phone app, mechanical). Put concrete thresholds in SW requirements where the problem allows (for example, "pump ON below 30 % soil moisture, OFF at 45 %").`;
}

export async function claudeSpec(p: ProblemData, kit: KitKey, design?: Ports): Promise<SpecData> {
  const client = new Anthropic();
  const facts = [
    `Problem in the student's words: ${p.story ?? ""}`,
    p.crop && `Crop / site: ${p.crop}`,
    p.area && `Area: ${p.area}`,
    p.water && `Water source: ${p.water}`,
    `Mains power at the site: ${p.power === "mains" ? "yes" : p.power === "solar" ? "no, solar only" : "no"}`,
    `Network at the site: ${p.network === "wifi" ? "WiFi" : p.network === "mobile" ? "mobile signal only" : "none"}`,
    p.climate && `Climate notes: ${p.climate}`,
    hasParts(design) && `The student has already chosen these parts in the Architecture stage. Describe exactly these, suggest exactly these ports, and add nothing else:\n${Object.entries(design).filter(([, b]) => b).map(([port, b]) => `${port}: ${b}`).join("\n")}`,
  ].filter(Boolean).join("\n");

  const res = await client.beta.messages.create({
    model: "claude-opus-5-5",
    max_tokens: 16000,
    betas: ["server-side-fallback-2026-07-01"],
    fallbacks: "default",
    output_config: { effort: "medium", format: { type: "json_schema", schema: SCHEMA as unknown as Record<string, unknown> } },
    system: systemPrompt(kit),
    messages: [{ role: "user", content: facts }],
  });

  if (res.stop_reason === "refusal") throw new Error("Claude declined to draft this spec.");
  const text = res.content.map((b) => (b.type === "text" ? b.text : "")).join("");
  const out = JSON.parse(text) as {
    what: string; made_of: string; use: string;
    requirements: Requirement[]; suggested_ports: { port: string; block: string }[];
  };

  // The rules decide: drop any suggestion that doesn't fit its port.
  const raw: Record<string, string> = {};
  for (const s of out.suggested_ports) {
    if (!raw[s.port] && !whyNot(kit, s.block, s.port)) raw[s.port] = s.block;
  }
  return {
    what: out.what,
    madeOf: out.made_of,
    use: out.use,
    safety: SAFETY,
    requirements: out.requirements.slice(0, 14),
    suggested: hasParts(design) ? cleanPorts(kit, design) : cleanPorts(kit, raw),
    source: "claude",
    generatedAt: new Date().toISOString(),
  };
}

export async function draftSpec(p: ProblemData, kit: KitKey, design?: Ports): Promise<SpecData> {
  if (!process.env.ANTHROPIC_API_KEY) {
    return templateSpec(p, kit, "Drafted from a template. Add an Anthropic API key on the server to have Claude draft it.", design);
  }
  try {
    return await claudeSpec(p, kit, design);
  } catch (e) {
    if (e instanceof Anthropic.RateLimitError) {
      return templateSpec(p, kit, "Claude is busy right now, so this was drafted from a template. Try again in a minute.", design);
    }
    if (e instanceof Anthropic.APIError) {
      console.error("studio spec: Claude API error", e.status, e.message);
    } else {
      console.error("studio spec:", e);
    }
    return templateSpec(p, kit, "Claude could not draft this one, so it was drafted from a template.", design);
  }
}
