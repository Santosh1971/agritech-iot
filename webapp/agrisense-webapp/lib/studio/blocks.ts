// Starter block library (docs/student-product-studio.md §3.2). Each block is
// one sensor or output a student can put on a port. This will move to
// common/blocks/<id>/block.yaml once the firmware drivers exist; the fields
// here are the ones the studio needs today.
import type { PortKind } from "./kits";

export type Block = {
  id: string;
  name: string;
  hindi: string;
  kind: PortKind; // which port type it plugs into
  signal: string; // analog, digital, 1-Wire, I²C address...
  supply: "3V3" | "5V" | "12V" | "—";
  mA3: number; // peak current on the 3.3 V rail
  mA5: number; // peak current on the 5 V rail
  output: boolean;
  why: string; // the "Why?" card
  i2cAddr?: string;
};

export const BLOCKS: Block[] = [
  { id: "soil", name: "Soil moisture (capacitive)", hindi: "मिट्टी की नमी", kind: "S", signal: "analog", supply: "3V3", mA3: 6, mA5: 0, output: false,
    why: "Capacitive probes measure the soil's dielectric constant without exposing metal to the soil. Resistive probes corrode within weeks." },
  { id: "soilt", name: "Soil temperature (DS18B20)", hindi: "मिट्टी का तापमान", kind: "S", signal: "1-Wire", supply: "3V3", mA3: 2, mA5: 0, output: false,
    why: "A sealed steel probe. Root growth and seed germination follow soil temperature more closely than air temperature." },
  { id: "float", name: "Tank float switch", hindi: "टंकी स्तर", kind: "S", signal: "digital", supply: "—", mA3: 0, mA5: 0, output: false,
    why: "A dry contact that closes when the water is above the float. It stops a pump running dry." },
  { id: "flow", name: "Water flow (hall sensor)", hindi: "पानी का बहाव", kind: "S", signal: "pulse", supply: "5V", mA3: 0, mA5: 15, output: false,
    why: "A small turbine with a magnet: each pulse is a fixed volume of water. It takes 5 V from the port's 5 V pin." },
  { id: "dht", name: "Air temperature + humidity (DHT22)", hindi: "हवा का तापमान और नमी", kind: "S", signal: "digital", supply: "3V3", mA3: 2, mA5: 0, output: false,
    why: "Cheap and simple. It can only be read every 2 seconds, which is plenty for weather." },
  { id: "rain", name: "Rain sensor", hindi: "बारिश", kind: "S", signal: "digital", supply: "3V3", mA3: 5, mA5: 0, output: false,
    why: "Lets a watering rule skip a cycle when it is already raining." },
  { id: "bme", name: "Air temperature, humidity, pressure (BME280)", hindi: "हवा का तापमान", kind: "I2C", signal: "I²C", i2cAddr: "0x76", supply: "3V3", mA3: 1, mA5: 0, output: false,
    why: "One I²C chip measures three things accurately. It sits on the student I²C bus, never on the RTC's bus." },
  { id: "light", name: "Light (BH1750)", hindi: "रोशनी", kind: "I2C", signal: "I²C", i2cAddr: "0x23", supply: "3V3", mA3: 1, mA5: 0, output: false,
    why: "Reports light in lux, the unit used for shade-net and greenhouse decisions." },
  { id: "oled", name: "OLED display 0.96\"", hindi: "डिस्प्ले", kind: "I2C", signal: "I²C", i2cAddr: "0x3C", supply: "3V3", mA3: 20, mA5: 0, output: true,
    why: "Shows readings at the device itself, with no phone needed." },
  { id: "pump", name: "Pump signal (via contactor box)", hindi: "पंप", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "The relay only signals our certified contactor box. 230 V never reaches the student board." },
  { id: "fogger", name: "Fogger or fan (12 V)", hindi: "फॉगर / पंखा", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "Low-voltage fogger pumps and fans can be switched directly by the relay contact." },
  { id: "valve", name: "Solenoid valve (12 V)", hindi: "वाल्व", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "Opens one drip line at a time, so one pump can water several beds in turn." },
  { id: "npk", name: "Soil NPK probe (RS-485)", hindi: "NPK", kind: "RS485", signal: "Modbus", supply: "12V", mA3: 0, mA5: 0, output: false,
    why: "An industrial probe that reports nitrogen, phosphorus and potassium over RS-485." },
  { id: "lora", name: "LoRa link to a Master", hindi: "LoRa", kind: "LORA", signal: "SPI", supply: "3V3", mA3: 0, mA5: 0, output: false,
    why: "Sends readings up to a few km to a WPC-style Master, with no WiFi or SIM needed." },
  { id: "gsm", name: "4G / SMS", hindi: "SMS", kind: "GSM", signal: "UART", supply: "—", mA3: 0, mA5: 0, output: false,
    why: "Sends SMS alerts and data from fields with mobile signal but no WiFi." },
];

export const BLOCK_BY_ID: Record<string, Block> = Object.fromEntries(BLOCKS.map((b) => [b.id, b]));
