// Lab 2A - Water flow with a YF-S201 (pulses -> L/min and litres)
// YF-S201: pulse frequency (Hz) ~= 7.5 x flow (L/min), about 450 pulses per litre.
//
// Real hardware wiring
//   Red -> 5V (VIN)   Black -> GND
//   Yellow (signal) -> 10k / 20k divider -> GPIO 27   (sensor gives 5 V pulses; ESP32 pins take 3.3 V)
//   Set SIMULATE_FLOW to 0.
//
// Wokwi has no flow sensor, so with SIMULATE_FLOW 1 the knob on GPIO 34 is the "tap":
// the sketch makes matching pulses on GPIO 25, which is wired to GPIO 27.

#define SIMULATE_FLOW 1

const int FLOW_PIN = 27;
volatile uint32_t pulses = 0;

void IRAM_ATTR onPulse() { pulses++; }   // runs on every pulse

float totalLitres = 0;
uint32_t lastMs = 0;

#if SIMULATE_FLOW
const int TAP_PIN = 34;       // potentiometer = how far the tap is open
const int FAKE_PULSE_PIN = 25;
#endif

void setup() {
  Serial.begin(115200);
  pinMode(FLOW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onPulse, FALLING);
#if SIMULATE_FLOW
  pinMode(FAKE_PULSE_PIN, OUTPUT);
#endif
  Serial.println("Lab 2A: flow meter ready");
}

void loop() {
#if SIMULATE_FLOW
  // Make fake sensor pulses: toggle GPIO 25 at 7.5 x (L/min) Hz
  static uint32_t lastToggleUs = 0, lastKnobMs = 0;
  static float hz = 0;
  if (millis() - lastKnobMs >= 200) {                     // read the tap knob 5x a second
    lastKnobMs = millis();
    float tapLpm = analogRead(TAP_PIN) * 30.0f / 4095.0f; // 0-30 L/min
    hz = tapLpm * 7.5f;
  }
  if (hz >= 1 && micros() - lastToggleUs >= (uint32_t)(500000.0f / hz)) {
    lastToggleUs = micros();
    digitalWrite(FAKE_PULSE_PIN, !digitalRead(FAKE_PULSE_PIN));
  }
#endif

  if (millis() - lastMs >= 1000) {        // once per second
    noInterrupts();
    uint32_t p = pulses;
    pulses = 0;
    interrupts();
    float lpm = p / 7.5f;                  // pulses in 1 s = Hz
    totalLitres += p / 450.0f;
    Serial.printf("Flow %.2f L/min   Total %.3f L\n", lpm, totalLitres);
    lastMs = millis();
  }
}
