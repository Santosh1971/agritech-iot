// Starter block library (docs/student-product-studio.md §3.2). Each block is
// one sensor or output a student can put on a port. Every id here except the
// Mega-only ones (npk, lora, gsm) must have a driver in the kit firmware
// (asc-studio-fw/src/design.cpp and blocks.cpp); ids are at most 9 characters. This will move to
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
  untested?: boolean; // driver written but not yet tried on real hardware
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
  { id: "probet", untested: true, name: "Temperature probe (DS18B20)", hindi: "तापमान प्रोब", kind: "S", signal: "1-Wire", supply: "3V3", mA3: 2, mA5: 0, output: false,
    why: "The same sealed steel probe as soil temperature, used in water tanks, cold rooms and grain bins. It reads down to −55 °C." },
  { id: "raing", untested: true, name: "Rain gauge (tipping bucket)", hindi: "वर्षा मापी", kind: "S", signal: "pulse", supply: "—", mA3: 0, mA5: 0, output: false,
    why: "Each tip of the little bucket is 0.28 mm of rain. The board adds up the last 24 hours, so a rule can skip watering after a good shower." },
  { id: "pir", untested: true, name: "Motion sensor (PIR)", hindi: "हलचल", kind: "S", signal: "digital", supply: "5V", mA3: 0, mA5: 1, output: false,
    why: "Sees the body heat of an animal or person moving. Use it to sound a siren when wild boar or cattle enter the field at night." },
  { id: "door", untested: true, name: "Door switch (magnetic)", hindi: "दरवाज़ा", kind: "S", signal: "digital", supply: "—", mA3: 0, mA5: 0, output: false,
    why: "A reed switch and a magnet. A cold-room door left open lets in heat and spoils stock, so an alert after a few minutes saves money." },
  { id: "tds", untested: true, name: "Water TDS / salts", hindi: "पानी में लवण", kind: "S", signal: "analog", supply: "3V3", mA3: 6, mA5: 0, output: false,
    why: "Measures dissolved salts in ppm. High-TDS borewell water harms crops, and in fertigation TDS tells you how strong the nutrient mix is." },
  { id: "level", untested: true, name: "Tank level (ultrasonic, A02YYUW)", hindi: "टंकी का स्तर", kind: "S", signal: "UART", supply: "3V3", mA3: 8, mA5: 0, output: false,
    why: "A waterproof ultrasonic sensor on the tank lid measures the distance down to the water. A bigger distance means less water. Only one fits per board." },
  { id: "sht", untested: true, name: "Air temperature + humidity (SHT31)", hindi: "हवा का तापमान और नमी", kind: "I2C", signal: "I²C", i2cAddr: "0x44", supply: "3V3", mA3: 2, mA5: 0, output: false,
    why: "More accurate than the DHT22 (±0.3 °C, ±2 % RH) and it lasts longer in damp air, so it suits greenhouses and cold rooms." },
  { id: "co2", untested: true, name: "CO₂ (SCD41)", hindi: "CO₂ गैस", kind: "I2C", signal: "I²C", i2cAddr: "0x62", supply: "3V3", mA3: 175, mA5: 0, output: false,
    why: "Plants use up CO₂ in a closed greenhouse; stored fruit and vegetables give it off. It also reports temperature and humidity." },
  { id: "irtemp", untested: true, name: "Leaf temperature (IR, MLX90614)", hindi: "पत्ती का तापमान", kind: "I2C", signal: "I²C", i2cAddr: "0x5A", supply: "3V3", mA3: 2, mA5: 0, output: false,
    why: "Reads the leaf's temperature without touching it. A thirsty plant closes its pores and its leaves warm above the air: crop water stress." },
  { id: "pump", name: "Pump signal (via contactor box)", hindi: "पंप", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "The relay only signals our certified contactor box. 230 V never reaches the student board." },
  { id: "fogger", name: "Fogger or fan (12 V)", hindi: "फॉगर / पंखा", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "Low-voltage fogger pumps and fans can be switched directly by the relay contact." },
  { id: "valve", name: "Solenoid valve (12 V)", hindi: "वाल्व", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "Opens one drip line at a time, so one pump can water several beds in turn." },
  { id: "valve24", untested: true, name: "Irrigation valve (24 V AC)", hindi: "सिंचाई वाल्व", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "The standard drip and sprinkler zone valve. It needs its own 24 V AC transformer; the relay only switches that low voltage." },
  { id: "siren", untested: true, name: "Siren or hooter (12 V)", hindi: "सायरन", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "Scares off animals, or warns that a tank is empty or a cold-room door is open." },
  { id: "doser", untested: true, name: "Dosing pump (12 V)", hindi: "खाद डोज़िंग पंप", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "A small peristaltic pump adds fertiliser or acid in measured doses, for fertigation and hydroponics." },
  { id: "fan", untested: true, name: "Exhaust fan (via contactor box)", hindi: "एग्ज़ॉस्ट पंखा", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "Big greenhouse and cold-room fans run on 230 V, so the relay only signals the contactor box." },
  { id: "growlite", untested: true, name: "Grow lights (via contactor box)", hindi: "ग्रो लाइट", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "Adds light on cloudy days or lengthens the day for nurseries. Switched through the contactor box, never on the board." },
  { id: "heater", untested: true, name: "Heater (via contactor box)", hindi: "हीटर", kind: "OUT", signal: "relay", supply: "5V", mA3: 0, mA5: 75, output: true,
    why: "Protects a nursery or greenhouse from frost. Heaters draw a lot of current, so always through the contactor box." },
  { id: "npk", name: "Soil NPK probe (RS-485)", hindi: "NPK", kind: "RS485", signal: "Modbus", supply: "12V", mA3: 0, mA5: 0, output: false,
    why: "An industrial probe that reports nitrogen, phosphorus and potassium over RS-485." },
  { id: "lora", name: "LoRa link to a Master", hindi: "LoRa", kind: "LORA", signal: "SPI", supply: "3V3", mA3: 0, mA5: 0, output: false,
    why: "Sends readings up to a few km to a WPC-style Master, with no WiFi or SIM needed." },
  { id: "gsm", name: "4G / SMS", hindi: "SMS", kind: "GSM", signal: "UART", supply: "—", mA3: 0, mA5: 0, output: false,
    why: "Sends SMS alerts and data from fields with mobile signal but no WiFi." },
];

export const BLOCK_BY_ID: Record<string, Block> = Object.fromEntries(BLOCKS.map((b) => [b.id, b]));
