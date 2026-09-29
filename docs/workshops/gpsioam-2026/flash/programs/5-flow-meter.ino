// 5 · Flow meter: count the pulses from the hall-effect flow sensor (YF-S201) and show
// litres per minute and total litres. Blow gently into the sensor to spin it.
// Wiring (ESP32 DevKit V1): sensor red -> 5V (VIN), black -> GND,
// yellow (signal) -> 10k/20k divider -> D35 (the sensor gives 5 V pulses; ESP32 pins take 3.3 V).
// YF-S201: about 7.5 pulses per second for each litre per minute, about 450 pulses per litre.

const int FLOW_PIN = 35;  // input-only pin, fine for a sensor
const int LED = 2;
const float PULSES_PER_LITRE = 450.0;

volatile uint32_t pulses = 0;
void IRAM_ATTR onPulse() { pulses++; }

uint32_t lastPulses = 0;
unsigned long lastPrint = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(FLOW_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onPulse, FALLING);
  Serial.println("Program 5: flow meter - blow into the sensor");
}

void loop() {
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    uint32_t total = pulses;
    uint32_t perSecond = total - lastPulses;
    lastPulses = total;
    float litresPerMin = perSecond / 7.5;
    float litres = total / PULSES_PER_LITRE;
    digitalWrite(LED, perSecond > 0 ? HIGH : LOW);  // LED on while something flows
    Serial.printf("Flow %.2f L/min   Total %.3f L   (%lu pulses)\n", litresPerMin, litres, (unsigned long)total);
  }
}
