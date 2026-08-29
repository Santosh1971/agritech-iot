// Water Manager-Mini — Hardware Bring-Up Diagnostic
//
// One sketch to flash for initial testing of the real 6-relay PCB,
// BEFORE wiring up the full RelayController/IrrigationController/
// Scheduler stack. Open Serial Monitor at 115200 baud, use the menu.
//
// Pin map per the spec (GPIO numbers, not dev-kit pin numbers):
//   Flow      : GPIO36 (VP)  — interrupt, pulse count
//   Press1    : GPIO39 (VN)  — ADC
//   Press2    : GPIO34       — ADC
//   Batt_Mon  : GPIO35       — ADC
//   IN2       : GPIO32       — ADC
//   IN3       : GPIO33       — ADC
//   IN1       : GPIO14       — digital, pulled up (HIGH=power OK, LOW=no power)
//   RLY_DATA  : GPIO17
//   RLY_CLK   : GPIO16
//   RLY_LATCH : GPIO13
//   RTC (DS1307) : I2C, SDA=GPIO21, SCL=GPIO22, address 0x68

#include <Arduino.h>
#include <Wire.h>

#define PIN_FLOW      36
#define PIN_PRESS1    39
#define PIN_PRESS2    34
#define PIN_BATT      35
#define PIN_IN2       32
#define PIN_IN3       33
#define PIN_IN1       14
#define PIN_RLY_DATA  17
#define PIN_RLY_CLK   16
#define PIN_RLY_LATCH 13
#define DS1307_ADDR   0x68

volatile uint32_t flowPulseCount = 0;
void IRAM_ATTR onFlowPulse() { flowPulseCount++; }

uint8_t relayState = 0;  // bit N = relay N (0=RL1/pump ... 5=RL6)

void pushRelayState() {
  digitalWrite(PIN_RLY_LATCH, LOW);
  shiftOut(PIN_RLY_DATA, PIN_RLY_CLK, MSBFIRST, relayState);
  digitalWrite(PIN_RLY_LATCH, HIGH);
}

void setRelay(uint8_t index, bool on) {
  if (index > 5) return;
  if (on) relayState |= (1 << index); else relayState &= ~(1 << index);
  pushRelayState();
}

// --- Raw DS1307 read/write (BCD), no external RTC library needed ---
uint8_t bcd2dec(uint8_t v) { return ((v / 16) * 10) + (v % 16); }
uint8_t dec2bcd(uint8_t v) { return ((v / 10) * 16) + (v % 10); }

bool ds1307Read(uint8_t &h, uint8_t &m, uint8_t &s) {
  Wire.beginTransmission(DS1307_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  Wire.requestFrom(DS1307_ADDR, 3);
  if (Wire.available() < 3) return false;
  s = bcd2dec(Wire.read() & 0x7F);
  m = bcd2dec(Wire.read());
  h = bcd2dec(Wire.read() & 0x3F);
  return true;
}

void ds1307Set(uint8_t h, uint8_t m, uint8_t s) {
  Wire.beginTransmission(DS1307_ADDR);
  Wire.write(0x00);
  Wire.write(dec2bcd(s));
  Wire.write(dec2bcd(m));
  Wire.write(dec2bcd(h));
  Wire.endTransmission();
}

void i2cScan() {
  Serial.println("Scanning I2C bus...");
  bool found = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X%s\n", addr,
                    addr == DS1307_ADDR ? "  <-- DS1307 RTC" : "");
      found = true;
    }
  }
  if (!found) Serial.println("  Nothing found — check SDA/SCL wiring.");
}

void testRelaysSequential() {
  Serial.println("Cycling relays 1-6 one at a time (2s each)...");
  for (uint8_t i = 0; i < 6; i++) {
    Serial.printf("  RL%u ON\n", i + 1);
    setRelay(i, true);
    delay(2000);
    setRelay(i, false);
    delay(300);
  }
  Serial.println("Done. All relays should now be off.");
}

void readPressureRaw(const char* label, uint8_t pin) {
  int raw = analogRead(pin);
  float volts = raw * (3.3f / 4095.0f);
  Serial.printf("  %s: raw=%d  volts=%.3f\n", label, raw, volts);
}

void printMenu() {
  Serial.println();
  Serial.println("--- Water Manager-Mini Bring-Up Menu ---");
  Serial.println("i - I2C scan (confirm DS1307 at 0x68)");
  Serial.println("t - Read RTC time");
  Serial.println("T - Set RTC time to compile time (once, then use 't' to confirm)");
  Serial.println("r - Cycle all 6 relays sequentially");
  Serial.println("1-6 - Toggle a single relay by number");
  Serial.println("f - Show flow pulse count (5s window)");
  Serial.println("p - Show pressure sensor 1/2 raw + voltage");
  Serial.println("b - Show battery monitor raw + voltage");
  Serial.println("n - Show IN1 (No Power) state");
  Serial.println("x - Show IN2/IN3 raw + voltage");
  Serial.println("a - Run full diagnostic (everything once)");
  Serial.println("-----------------------------------------");
}

void runFullDiagnostic() {
  i2cScan();
  uint8_t h, m, s;
  if (ds1307Read(h, m, s)) Serial.printf("RTC time: %02u:%02u:%02u\n", h, m, s);
  else Serial.println("RTC read FAILED");

  Serial.println("Pressure sensors:");
  readPressureRaw("Press1", PIN_PRESS1);
  readPressureRaw("Press2", PIN_PRESS2);
  Serial.println("Battery:");
  readPressureRaw("Batt_Mon", PIN_BATT);
  Serial.println("IN2/IN3:");
  readPressureRaw("IN2", PIN_IN2);
  readPressureRaw("IN3", PIN_IN3);
  Serial.printf("IN1 (No Power sense): %s (%s)\n",
                digitalRead(PIN_IN1) ? "HIGH" : "LOW",
                digitalRead(PIN_IN1) ? "power OK" : "NO POWER");
  Serial.printf("Flow pulses so far: %u\n", flowPulseCount);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_RLY_DATA, OUTPUT);
  pinMode(PIN_RLY_CLK, OUTPUT);
  pinMode(PIN_RLY_LATCH, OUTPUT);
  pushRelayState();  // all relays off at boot — fail-safe

  pinMode(PIN_IN1, INPUT);  // externally pulled up per schematic
  pinMode(PIN_FLOW, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW), onFlowPulse, RISING);

  Wire.begin(21, 22);  // SDA, SCL

  printMenu();
}

void loop() {
  if (!Serial.available()) { delay(20); return; }
  char c = Serial.read();

  switch (c) {
    case 'i': i2cScan(); break;
    case 't': {
      uint8_t h, m, s;
      if (ds1307Read(h, m, s)) Serial.printf("RTC time: %02u:%02u:%02u\n", h, m, s);
      else Serial.println("RTC read FAILED — check wiring/I2C scan first");
      break;
    }
    case 'T': {
      // Sets to a fixed placeholder time — replace with real time as needed.
      ds1307Set(12, 0, 0);
      Serial.println("RTC set to 12:00:00 (placeholder) — press 't' to confirm.");
      break;
    }
    case 'r': testRelaysSequential(); break;
    case '1': case '2': case '3': case '4': case '5': case '6': {
      uint8_t idx = c - '1';
      bool newState = !((relayState >> idx) & 1);
      setRelay(idx, newState);
      Serial.printf("RL%u -> %s\n", idx + 1, newState ? "ON" : "OFF");
      break;
    }
    case 'f': {
      flowPulseCount = 0;
      Serial.println("Counting flow pulses for 5s...");
      delay(5000);
      Serial.printf("  Pulses: %u\n", flowPulseCount);
      break;
    }
    case 'p':
      readPressureRaw("Press1", PIN_PRESS1);
      readPressureRaw("Press2", PIN_PRESS2);
      break;
    case 'b': readPressureRaw("Batt_Mon", PIN_BATT); break;
    case 'n':
      Serial.printf("IN1: %s (%s)\n", digitalRead(PIN_IN1) ? "HIGH" : "LOW",
                    digitalRead(PIN_IN1) ? "power OK" : "NO POWER");
      break;
    case 'x':
      readPressureRaw("IN2", PIN_IN2);
      readPressureRaw("IN3", PIN_IN3);
      break;
    case 'a': runFullDiagnostic(); break;
    default: printMenu(); break;
  }
}
