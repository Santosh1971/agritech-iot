/*
 * FG1 Test Jig Controller
 *
 * Runs on a small permanent fixture MCU (Arduino Nano / ESP8266) --
 * NOT the DUT. Implements the serial protocol in
 * docs/testing/TEST_JIG_SPEC.md section 4, driven by
 * testing/jig_controller.py on the test PC.
 *
 * Wiring:
 *   PULSE_OUT_PIN   -> jumper into DUT's flow sensor input (GPIO35)
 *   RELAY_SENSE_PIN -> voltage divider off the DUT's relay output
 *                      indicator circuit (see spec section 2.4)
 *   WIFI_LED_SENSE_PIN, FLOW_LED_SENSE_PIN -> optional phototransistors
 *                      positioned over the DUT's onboard LEDs (spec 2.3)
 *                      -- leave unpopulated and these just always read LOW
 */

const int PULSE_OUT_PIN      = 5;
const int RELAY_SENSE_PIN    = A0;
const int WIFI_LED_SENSE_PIN = A1;  // optional, see header comment
const int FLOW_LED_SENSE_PIN = A2;  // optional, see header comment

// Threshold for the relay/LED sense ADC readings -- tune once the
// physical divider values are finalized. 512 is the midpoint for a
// 10-bit ADC (0-1023), a reasonable starting guess for a roughly
// symmetric divider.
const int SENSE_THRESHOLD = 512;

// Pulse timing -- fast enough for a quick test, slow enough to be a
// clean, unambiguous digital edge for the DUT's flow sensor interrupt.
const int PULSE_HIGH_US = 500;
const int PULSE_LOW_US  = 500;

String inputLine;

void setup() {
  Serial.begin(115200);
  pinMode(PULSE_OUT_PIN, OUTPUT);
  digitalWrite(PULSE_OUT_PIN, LOW);
  pinMode(RELAY_SENSE_PIN, INPUT);
  pinMode(WIFI_LED_SENSE_PIN, INPUT);
  pinMode(FLOW_LED_SENSE_PIN, INPUT);
}

void emitPulses(int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(PULSE_OUT_PIN, HIGH);
    delayMicroseconds(PULSE_HIGH_US);
    digitalWrite(PULSE_OUT_PIN, LOW);
    delayMicroseconds(PULSE_LOW_US);
  }
}

void handleCommand(const String& line) {
  if (line == "PING") {
    Serial.println("PONG");
    return;
  }

  if (line == "RELAY?") {
    bool on = analogRead(RELAY_SENSE_PIN) > SENSE_THRESHOLD;
    Serial.println(on ? "RELAY:ON" : "RELAY:OFF");
    return;
  }

  if (line == "LED:wifi?") {
    bool on = analogRead(WIFI_LED_SENSE_PIN) > SENSE_THRESHOLD;
    Serial.println(on ? "LED:ON" : "LED:OFF");
    return;
  }

  if (line == "LED:flow?") {
    bool on = analogRead(FLOW_LED_SENSE_PIN) > SENSE_THRESHOLD;
    Serial.println(on ? "LED:ON" : "LED:OFF");
    return;
  }

  if (line.startsWith("PULSE:")) {
    int count = line.substring(6).toInt();
    if (count > 0) {
      emitPulses(count);
      Serial.print("OK:");
      Serial.println(count);
    } else {
      Serial.println("ERR:bad count");
    }
    return;
  }

  Serial.println("ERR:unknown command");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      inputLine.trim();
      if (inputLine.length() > 0) handleCommand(inputLine);
      inputLine = "";
    } else if (c != '\r') {
      inputLine += c;
    }
  }
}
