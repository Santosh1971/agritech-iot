// Product catalogue for the public website. One entry = one page at
// /products/<slug>. Text sticks to what the products do today; anything still
// being built is marked with status "In development".

export type Family = "Water & pumps" | "Sasya: sensing & climate";

export type Product = {
  slug: string;
  name: string;
  short: string; // the name as used in running text
  family: Family;
  line: string; // one-line promise for cards
  problem: string;
  answer: string;
  how: { step: string; detail: string }[];
  features: string[];
  forWhom: string[];
  status?: string;
  icon: "drop" | "pump" | "valve" | "soil" | "leaf" | "snow" | "fish" | "clock";
};

export const products: Product[] = [
  {
    slug: "flowguard",
    name: "FG1 FlowGuard",
    short: "FlowGuard",
    family: "Water & pumps",
    icon: "drop",
    line: "Measured watering for nurseries and small farms, even without internet.",
    problem:
      "Nurseries and small plots are watered by hand. Forget one day and seedlings wilt; water too long and it is wasted; travel and nobody waters at all.",
    answer:
      "FlowGuard waters on a schedule, by litres or by minutes, and its flow sensor confirms the water really reached the plants.",
    how: [
      { step: "Set the schedule", detail: "Up to 4 waterings a day, each by litres or by minutes, from the phone app." },
      { step: "It waters on time", detail: "The controller switches the pump or valve on schedule, with or without internet." },
      { step: "Flow confirms it", detail: "The flow sensor counts the litres delivered and shows today's water in the app." },
    ],
    features: [
      "Up to 4 waterings a day, by volume (litres) or by time",
      "Flow sensor measures the water actually delivered",
      "Runs its schedule without internet",
      "Phone app: start, stop and check today's water",
      "Remote access when the nursery has WiFi",
    ],
    forWhom: ["Nurseries", "Polyhouses", "Small farms and kitchen gardens"],
  },
  {
    slug: "wpc",
    name: "WPC Wireless Pump Controller",
    short: "WPC",
    family: "Water & pumps",
    icon: "pump",
    line: "Up to 20 pumps across a field, run from one master, without cables.",
    problem:
      "Many farms feed one sump from several borewells spread across the field. Power comes in fixed slots, often at night, so someone walks from pump to pump to switch them.",
    answer:
      "WPC reads the sump level and switches each pump automatically over long-range LoRa radio. No cables between pumps, and the farmer sees every pump on the phone.",
    how: [
      { step: "Master reads the sump", detail: "Float switches at up to 3 levels tell the master how full the sump is." },
      { step: "Pumps switch by radio", detail: "The master turns each pump node on or off over LoRa, through the pump's existing starter." },
      { step: "Farmer sees it all", detail: "The app shows each pump, power and water flow, locally or over the internet." },
    ],
    features: [
      "One master controls up to 20 pump nodes",
      "Long-range LoRa radio: no cables between pumps",
      "Automatic on/off by sump level, with manual override from the app",
      "Power and water-flow status for every pump",
      "Fail-safe: a pump node switches off if it loses contact with the master",
      "Works locally; internet monitoring over farm WiFi",
    ],
    forWhom: ["Farms with several borewells feeding one sump", "Orchards and plantations", "FPO and community water systems"],
  },
  {
    slug: "watermaster",
    name: "WaterMaster",
    short: "WaterMaster",
    family: "Water & pumps",
    icon: "valve",
    line: "Drip and fertigation on a schedule, zone by zone, on solar power.",
    problem:
      "Drip systems are switched by hand: zones run too long or too short, fertiliser goes on unevenly, and a power cut breaks the routine.",
    answer:
      "WaterMaster runs the pump, the fertiliser doser and each valve zone on schedules the farmer sets, measures flow and pressure, and keeps going on solar and battery power.",
    how: [
      { step: "Plan the zones", detail: "Choose valves, run time and dosing; repeat daily, on alternate days or every few days." },
      { step: "It runs the farm", detail: "The pump starts only with a valve open; dosing runs inside the irrigation cycle." },
      { step: "Track and alert", detail: "Flow, pressure and power are logged; the app shows water and fertiliser applied per zone." },
    ],
    features: [
      "Pump, fertiliser doser and up to 4 valve zones (WM1)",
      "Daily, alternate-day or every-N-day schedules, including overnight runs",
      "Flow and pressure sensing, with pressure alerts",
      "Runs schedules without internet; pauses and resumes after power cuts",
      "Solar and battery powered",
      "One app for farmer, dealer and admin",
      "WM2: larger farms, 4 flow inputs, latching valves, remote updates (in development)",
    ],
    forWhom: ["Drip and fertigation farms", "Horticulture and plantations", "Dealers who service many farms"],
  },
  {
    slug: "sasya-smart-irrigation",
    name: "Sasya Smart Irrigation",
    short: "Smart Irrigation",
    family: "Sasya: sensing & climate",
    icon: "soil",
    line: "Drip that starts when the plant needs water, and stops by itself.",
    problem: "Fixed drip timers water by the clock, not by what the soil and the crop need.",
    answer:
      "Soil moisture sensors at several depths tell the controller when the root zone is dry; it turns the drip on and off automatically, using moisture tables for each crop and stage.",
    how: [
      { step: "Sense the soil", detail: "Moisture sensors at surface, root zone and drainage level." },
      { step: "Decide by crop", detail: "Trigger points from soil moisture tables for many drip-irrigated crops." },
      { step: "Switch the pump", detail: "The gateway controller turns pumps and zones on and off." },
    ],
    features: [
      "Multi-level soil moisture (pF) sensors",
      "Battery and solar powered sensor nodes",
      "Gateway cum pump controller: the local brain",
      "WiFi, GSM, NB-IoT or LoRa connectivity",
      "Multi-zone relays for pumps",
    ],
    forWhom: ["Drip-irrigated farms", "Orchards and vineyards", "Research and demo plots"],
  },
  {
    slug: "sasya-climate-control",
    name: "Sasya Smart Climate Control",
    short: "Smart Climate Control",
    family: "Sasya: sensing & climate",
    icon: "leaf",
    line: "Greenhouse climate, irrigation and fertigation, measured and automated.",
    problem:
      "A greenhouse crop depends on temperature, humidity, CO2, light and root-zone moisture, which change through the day faster than anyone can watch.",
    answer:
      "Wireless sensors in each zone feed a gateway that compares readings with your rules and switches fans, pads, irrigation and fertigation through your existing panels.",
    how: [
      { step: "Measure every zone", detail: "Temperature, humidity, soil pF, CO2, light, water pH and TDS; wind and rain outside." },
      { step: "Apply your rules", detail: "Trigger points and a rules table on the cloud platform, with manual overrides." },
      { step: "Automate the panel", detail: "Relays wired into existing switch boards run fans, pads, pumps and dosing." },
    ],
    features: [
      "Multi-zone environment sensors and wireless nodes",
      "Outside weather node: wind and rain",
      "Gateway with local display and multi-channel relays",
      "Automation bus into existing control panels",
      "Cloud platform: data, rules, triggers, reports, manual override",
    ],
    forWhom: ["Polyhouses and greenhouses", "Hydroponic and vertical farms", "Nurseries"],
  },
  {
    slug: "sasya-cold-storage",
    name: "Sasya Smart Cold Storage",
    short: "Smart Cold Storage",
    family: "Sasya: sensing & climate",
    icon: "snow",
    line: "Temperature, humidity and CO2 in every zone of a cold store, on your phone.",
    problem:
      "A cold store that drifts out of range spoils produce, wastes power and wears out machines, often before the operator notices.",
    answer:
      "Sensors in each zone track temperature, humidity and CO2; readings show on a local display, the office computer and the phone, with alerts when action is needed.",
    how: [
      { step: "Sensors at every level", detail: "Temperature and humidity at all levels, CO2 at ground level." },
      { step: "See it anywhere", detail: "Local display for the operator, plus office computer and mobile, 24x7." },
      { step: "Act on alerts", detail: "Notifications when a zone moves out of range." },
    ],
    features: [
      "Extendable multi-zone sensor banks",
      "Local, office and mobile displays with alerts",
      "Wireless link between controller and machine room",
      "WiFi, GSM, NB-IoT or LoRa connectivity",
      "Records that help prove storage quality to buyers",
    ],
    forWhom: ["Cold storages", "Ripening chambers", "Onion and grain stores"],
  },
  {
    slug: "sasya-biofloc",
    name: "Sasya Biofloc Culture Management",
    short: "Biofloc Culture Management",
    family: "Sasya: sensing & climate",
    icon: "fish",
    line: "Water quality for fish and shrimp tanks, tracked live with mobile alerts.",
    problem:
      "In dense biofloc tanks, fish and shrimp health depends on water quality that can turn in hours.",
    answer:
      "A low-cost, Indian-designed sensor system measures the key water parameters in every tank and alerts the farmer on the phone, even during a power cut.",
    how: [
      { step: "Measure each tank", detail: "pH, EC, temperature, nitrates, ammonia and dissolved oxygen." },
      { step: "One gateway", detail: "Sensor nodes for many tanks connect to the same gateway." },
      { step: "Alert and act", detail: "Mobile alerts, with provision to automate aerators and feeders." },
    ],
    features: [
      "Multi-tank sensor nodes on one gateway",
      "Measure, alert and provision for automation",
      "Solar powered: keeps tracking during power failures",
      "WiFi connected: programmed, monitored and fixed remotely",
    ],
    forWhom: ["Biofloc fish and shrimp farms", "Hatcheries", "Aquaculture training centres"],
  },
  {
    slug: "sasya-samaya-scheduler",
    name: "Samaya Smart Scheduler",
    short: "Samaya Scheduler",
    family: "Sasya: sensing & climate",
    icon: "clock",
    line: "Up to 100 schedules a day for pumps, venturis and valves.",
    problem:
      "Farms and greenhouses run many devices in a set order, and power cuts break the sequence with nobody sure what ran.",
    answer:
      "Samaya runs complex schedules on 12 to 24 relay channels, resumes after a power cut, and shows on the phone whether each device really switched.",
    how: [
      { step: "Set the order", detail: "Up to 100 schedules a day across all channels, by phone or computer." },
      { step: "It runs reliably", detail: "Each channel independent, with manual on/off; resumes when power returns." },
      { step: "Track remotely", detail: "Experts can configure, monitor and troubleshoot it over WiFi." },
    ],
    features: [
      "12 relay channels, extendable to 24",
      "Up to 100 schedules a day",
      "Independent programming and manual on/off per channel",
      "Remaining programme resumes after a power failure",
      "Remote setup and monitoring over WiFi",
    ],
    forWhom: ["Greenhouses", "Fertigation units", "Farms with many pumps and valves"],
  },
];

export const families: Family[] = ["Water & pumps", "Sasya: sensing & climate"];

export function productBySlug(slug: string) {
  return products.find((p) => p.slug === slug);
}
