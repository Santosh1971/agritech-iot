// 6 · Pump with flow check: the relay is the pump. Press BOOT (or type ON / OFF) to switch it.
// If no water flows for 10 seconds after switching on, the pump is stopped to protect the motor
// ("dry run"). Blow into the flow sensor to keep it running. Each run is printed with litres used.
// Wiring (ESP32 DevKit V1): relay IN -> D19 (VCC -> 5V, GND -> GND); flow sensor signal -> D35
// through a 10k/20k divider (red -> 5V, black -> GND). Blue LED (GPIO 2) shows the pump state.

const int RELAY = 19;
const int FLOW_PIN = 35;
const int BUTTON = 0;
const int LED = 2;
const bool RELAY_ACTIVE_LOW = true;       // if the relay works the wrong way round, change to false
const unsigned long NO_FLOW_STOP_MS = 10000;
const uint32_t MIN_PULSES = 5;            // pulses needed in 10 s to count as "water is flowing"
const float PULSES_PER_LITRE = 450.0;

volatile uint32_t pulses = 0;
void IRAM_ATTR onPulse() { pulses++; }

bool pumpOn = false;
unsigned long startedAt = 0, windowStart = 0;
uint32_t startPulses = 0, windowPulses = 0;
bool lastPressed = false;

void setRelay(bool on) { digitalWrite(RELAY, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW); }

void pump(bool on, const char *reason) {
  if (on == pumpOn) return;
  pumpOn = on;
  setRelay(on);
  digitalWrite(LED, on ? HIGH : LOW);
  if (on) {
    startedAt = windowStart = millis();
    startPulses = windowPulses = pulses;
    Serial.printf("PUMP ON (%s). Checking for flow...\n", reason);
  } else {
    float litres = (pulses - startPulses) / PULSES_PER_LITRE;
    Serial.printf("PUMP OFF (%s). Ran %lu s, %.2f litres\n", reason, (millis() - startedAt) / 1000, litres);
  }
}

void setup() {
  Serial.begin(115200);
  setRelay(false);  // make sure the pump starts OFF
  pinMode(RELAY, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(FLOW_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onPulse, FALLING);
  Serial.println("Program 6: press BOOT or type ON / OFF to switch the pump");
}

void loop() {
  bool pressed = digitalRead(BUTTON) == LOW;
  if (pressed && !lastPressed) { pump(!pumpOn, "button"); delay(30); }
  lastPressed = pressed;

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    if (cmd == "ON") pump(true, "command");
    if (cmd == "OFF") pump(false, "command");
  }

  if (pumpOn && millis() - windowStart >= NO_FLOW_STOP_MS) {  // dry-run protection
    if (pulses - windowPulses < MIN_PULSES) {
      Serial.println("NO FLOW - pump stopped to protect the motor");
      pump(false, "no flow");
    } else {
      Serial.printf("Water flowing: %.2f litres so far\n", (pulses - startPulses) / PULSES_PER_LITRE);
      windowStart = millis();
      windowPulses = pulses;
    }
  }
}
