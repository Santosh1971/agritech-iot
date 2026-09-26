// Lab 2B - Soil moisture + tank float switch + automatic pump
// Pump turns ON below 35 % moisture, OFF above 60 % (hysteresis stops relay chatter),
// and never runs when the tank float says EMPTY (safety interlock).
//
// Wiring
//   Capacitive soil sensor AOUT -> GPIO 34   (Wokwi: the knob)
//   Float switch -> GPIO 14 and GND          (Wokwi: slide switch; left = tank has water)
//   Relay module IN -> GPIO 26, VCC -> 5V (VIN), GND -> GND
//   Pump LED (+ 220 ohm) -> GPIO 26          (use an LED or a small 5 V pump - no mains!)
//
// Use ADC1 pins (32-39) for analog: ADC2 pins stop working when WiFi is on.
// Many relay modules are active-LOW: if yours is, set RELAY_ACTIVE_HIGH to false.

const int SOIL_PIN  = 34;
const int FLOAT_PIN = 14;
const int PUMP_PIN  = 26;
const bool RELAY_ACTIVE_HIGH = true;

const int DRY = 3000;       // raw reading in dry soil  - calibrate with your sensor!
const int WET = 1300;       // raw reading in water     - calibrate with your sensor!
const int ON_BELOW  = 35;   // % moisture: start watering
const int OFF_ABOVE = 60;   // % moisture: stop watering

bool pumpOn = false;

void setup() {
  Serial.begin(115200);
  pinMode(FLOAT_PIN, INPUT_PULLUP);   // switch closed = LOW
  pinMode(PUMP_PIN, OUTPUT);
  Serial.println("Lab 2B: auto pump ready");
}

void loop() {
  int raw = analogRead(SOIL_PIN);
  int pct = constrain(map(raw, DRY, WET, 0, 100), 0, 100);
  bool tankHasWater = (digitalRead(FLOAT_PIN) == LOW);

  if (pct < ON_BELOW)  pumpOn = true;
  if (pct > OFF_ABOVE) pumpOn = false;
  if (!tankHasWater)   pumpOn = false;   // safety interlock wins

  digitalWrite(PUMP_PIN, (pumpOn == RELAY_ACTIVE_HIGH) ? HIGH : LOW);
  Serial.printf("Soil %d%% (raw %d)   Tank %s   Pump %s\n",
                pct, raw, tankHasWater ? "OK" : "EMPTY", pumpOn ? "ON" : "OFF");
  delay(1000);
}
