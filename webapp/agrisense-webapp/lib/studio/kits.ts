// The student kits' ports, read from the same pin map the hardware uses.
// lib/studio/pinmap.json is a copy of products/ASC-StudentKit/hardware/pinmap.json;
// check_pinmap.py fails if the two ever differ.
import pinmap from "./pinmap.json";

export type KitKey = "MINI" | "MEGA";
export type PortKind = "S" | "I2C" | "OUT" | "RS485" | "LORA" | "GSM";

export type Port = {
  id: string; // what students see: S1, I2C-1, OUT1, RS485...
  kind: PortKind;
  pins: string; // Engineer's view only (SYS-04): students never see GPIO numbers
};

type Board = { sensor_ports: number; i2c_ports: number; relays: number; pins: Record<string, string> };
const boards = pinmap.boards as unknown as Record<"mini" | "mega", Board>;
const adc1 = new Set(pinmap.adc1);

function gpioOf(board: Board, signal: string): number | undefined {
  const hit = Object.entries(board.pins).find(([, s]) => s === signal);
  return hit ? Number(hit[0]) : undefined;
}

function buildPorts(key: "mini" | "mega"): Port[] {
  const b = boards[key];
  const ports: Port[] = [];
  for (let i = 1; i <= b.sensor_ports; i++) {
    const g = gpioOf(b, `S${i}`);
    ports.push({ id: `S${i}`, kind: "S", pins: `GPIO${g}${g !== undefined && adc1.has(g) ? ` · ADC1_CH${g - 1}` : ""}` });
  }
  const sda = gpioOf(b, "I2C_EXT_SDA"), scl = gpioOf(b, "I2C_EXT_SCL");
  for (let i = 1; i <= b.i2c_ports; i++) {
    ports.push({ id: `I2C-${i}`, kind: "I2C", pins: `SDA GPIO${sda} · SCL GPIO${scl} (shared bus)` });
  }
  for (let i = 1; i <= b.relays; i++) {
    ports.push({ id: `OUT${i}`, kind: "OUT", pins: `GPIO${gpioOf(b, `OUT${i}`)} → relay driver` });
  }
  if (key === "mega") {
    ports.push({ id: "RS485", kind: "RS485", pins: `TX GPIO${gpioOf(b, "RS485_TX")} · RX GPIO${gpioOf(b, "RS485_RX")}` });
    ports.push({ id: "LORA", kind: "LORA", pins: `SX1262 · NSS GPIO${gpioOf(b, "LORA_NSS")} (AWD1 map)` });
    ports.push({ id: "GSM", kind: "GSM", pins: `UART GPIO${gpioOf(b, "GSM_TX")}/${gpioOf(b, "GSM_RX")}` });
  }
  return ports;
}

export const KITS: Record<KitKey, { name: string; blurb: string; ports: Port[] }> = {
  MINI: {
    name: "Mini",
    blurb: "Classroom kit: mains or USB power, WiFi and Bluetooth, RTC, 4 sensor ports, 1 I²C port, 2 relays.",
    ports: buildPorts("mini"),
  },
  MEGA: {
    name: "Mega",
    blurb: "Field kit: battery and solar, 8 sensor ports, 2 I²C ports, 4 relays, RS-485, LoRa and 4G slots.",
    ports: buildPorts("mega"),
  },
};

export function portOf(kit: KitKey, id: string): Port | undefined {
  return KITS[kit].ports.find((p) => p.id === id);
}
