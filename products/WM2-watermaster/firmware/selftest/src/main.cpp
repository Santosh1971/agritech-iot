// WM2-WaterMaster hardware self-test.
//
// At boot it runs every automatic check once and prints a T_PASS/T_WARN/T_FAIL
// summary, then sits at a serial menu (115200 baud, press h) for the
// checks that need a person watching: LED walk, live sensor monitor,
// valve pulses.
//
// Nothing here is product logic -- it only proves each block of the board
// works: MCU, 12V monitor, RTC, LEDs, sensor inputs, valve drivers, WiFi RF.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_mac.h>
#include "wm2_pins.h"

using namespace wm2;

#define FW_NAME "WM2 selftest 0.1"

// ---------------------------------------------------------------- results

enum Status : uint8_t { T_PASS, T_WARN, T_FAIL, T_SKIP };
static const char* statusText(Status s) {
  switch (s) {
    case T_PASS: return "PASS";
    case T_WARN: return "WARN";
    case T_FAIL: return "FAIL";
    default:   return "SKIP";
  }
}

struct Result {
  const char* name;
  Status status;
  char detail[100];
};
static Result results[12];
static uint8_t resultCount = 0;

static void report(const char* name, Status s, const char* fmt, ...) {
  Result* r = nullptr;
  for (uint8_t i = 0; i < resultCount; i++)
    if (strcmp(results[i].name, name) == 0) r = &results[i];
  if (!r && resultCount < sizeof(results) / sizeof(results[0])) r = &results[resultCount++];
  if (!r) return;
  r->name = name;
  r->status = s;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(r->detail, sizeof(r->detail), fmt, ap);
  va_end(ap);
  Serial.printf("  -> [%s] %s: %s\n", statusText(s), name, r->detail);
}

// ---------------------------------------------------------------- helpers

static void allOutputsSafe() {
  // Valve drivers first: both inputs LOW = coast/sleep.
  const uint8_t drv[] = {PIN_V1_IN1, PIN_V1_IN2, PIN_V2_IN1, PIN_V2_IN2};
  for (uint8_t p : drv) { digitalWrite(p, LOW); pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  for (uint8_t p : PIN_LED_FLOW) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  pinMode(PIN_LED_PS, OUTPUT);   digitalWrite(PIN_LED_PS, LOW);
  pinMode(PIN_LED_WIFI, OUTPUT); digitalWrite(PIN_LED_WIFI, LOW);
}

static void allLeds(bool on) {
  for (uint8_t p : PIN_LED_FLOW) digitalWrite(p, on);
  digitalWrite(PIN_LED_PS, on);
  digitalWrite(PIN_LED_WIFI, on);
}

static float readBatteryVolts() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(PIN_BATT_ADC);
  return (mv / 16.0f) * BATT_DIVIDER / 1000.0f;
}

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static bool rtcRead(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)RTC_I2C_ADDR, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static bool rtcWrite(uint8_t reg, const uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(reg);
  Wire.write(buf, n);
  return Wire.endTransmission() == 0;
}

static void rtcPrintNow() {
  uint8_t t[7];
  if (!rtcRead(0x00, t, 7)) { Serial.println("  RTC read failed"); return; }
  Serial.printf("  RTC time: 20%02u-%02u-%02u %02u:%02u:%02u%s\n",
                bcd2dec(t[6]), bcd2dec(t[5] & 0x1F), bcd2dec(t[4] & 0x3F),
                bcd2dec(t[2] & 0x3F), bcd2dec(t[1] & 0x7F), bcd2dec(t[0] & 0x7F),
                (t[0] & 0x80) ? "  (CLOCK HALTED)" : "");
}

// Set the RTC to this firmware's build time (good enough for a bench test).
static bool rtcSetToBuildTime() {
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {0};
  int day, year, hh, mm, ss;
  sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
  int month = (strstr(months, mon) - months) / 3 + 1;
  uint8_t t[7] = {dec2bcd(ss), dec2bcd(mm), dec2bcd(hh), 1,
                  dec2bcd(day), dec2bcd(month), dec2bcd(year % 100)};  // CH bit 0 = run
  return rtcWrite(0x00, t, 7);
}

// ---------------------------------------------------------------- pulse counting

static volatile uint32_t flowCount[4] = {0, 0, 0, 0};
static void IRAM_ATTR isrFlow0() { flowCount[0]++; }
static void IRAM_ATTR isrFlow1() { flowCount[1]++; }
static void IRAM_ATTR isrFlow2() { flowCount[2]++; }
static void IRAM_ATTR isrFlow3() { flowCount[3]++; }
static void (*const flowIsr[4])() = {isrFlow0, isrFlow1, isrFlow2, isrFlow3};

// ---------------------------------------------------------------- tests

static void testChip() {
  Serial.println("\n[1] MCU");
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("  %s rev %d, %d cores @ %lu MHz, flash %lu MB, free heap %lu\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz(),
                (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned long)ESP.getFreeHeap());
  Serial.printf("  MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  bool ok = ESP.getFlashChipSize() >= 4 * 1024 * 1024;
  report("MCU", ok ? T_PASS : T_WARN, "%s, %lu MB flash", ESP.getChipModel(),
         (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)));
}

static float lastBattV = 0;

static void testBattery() {
  Serial.println("\n[2] 12V supply monitor (IO33)");
  lastBattV = readBatteryVolts();
  Serial.printf("  Vin = %.2f V (ADC pin %.0f mV)\n", lastBattV,
                lastBattV / BATT_DIVIDER * 1000.0f);
  if (lastBattV < 2.0f)
    report("12V monitor", T_WARN, "%.2f V - no 12V input? (USB 5V only: valves won't drive)", lastBattV);
  else if (lastBattV > 15.5f)
    report("12V monitor", T_FAIL, "%.2f V - too high, check divider R23/R24", lastBattV);
  else if (lastBattV < 10.5f)
    report("12V monitor", T_WARN, "%.2f V - low", lastBattV);
  else
    report("12V monitor", T_PASS, "%.2f V", lastBattV);
}

static void testRtc() {
  Serial.println("\n[3] I2C bus + DS1307 RTC");
  uint8_t found = 0;
  bool rtcSeen = false;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  I2C device at 0x%02X\n", a);
      found++;
      if (a == RTC_I2C_ADDR) rtcSeen = true;
    }
  }
  if (!rtcSeen) {
    report("RTC", T_FAIL, "no ACK at 0x68 (%u other) - U5 not fitted yet, or check 5V/SDA/SCL", found);
    return;
  }

  // NVRAM write/read-back on the last RAM byte (restored afterwards).
  uint8_t orig, rb;
  bool ramOk = rtcRead(0x3F, &orig, 1);
  for (uint8_t pat : {(uint8_t)0xA5, (uint8_t)0x5A}) {
    ramOk = ramOk && rtcWrite(0x3F, &pat, 1) && rtcRead(0x3F, &rb, 1) && rb == pat;
  }
  rtcWrite(0x3F, &orig, 1);

  uint8_t t[7];
  if (!rtcRead(0x00, t, 7)) { report("RTC", T_FAIL, "register read failed"); return; }
  bool halted = t[0] & 0x80;
  bool invalid = bcd2dec(t[5] & 0x1F) == 0 || bcd2dec(t[5] & 0x1F) > 12 || bcd2dec(t[6]) < 24;
  bool wasReset = halted || invalid;
  if (wasReset) {
    Serial.println("  RTC was halted/unset (new chip or backup cell lost) - setting to build time");
    rtcSetToBuildTime();
  }
  rtcPrintNow();

  // Is the 32.768 kHz crystal actually running?
  uint8_t s1, s2;
  rtcRead(0x00, &s1, 1);
  delay(1100);
  rtcRead(0x00, &s2, 1);
  bool ticking = (s1 & 0x7F) != (s2 & 0x7F);

  if (!ticking)
    report("RTC", T_FAIL, "seconds not advancing - check crystal Y1");
  else if (!ramOk)
    report("RTC", T_FAIL, "clock ticks but NVRAM read-back failed");
  else if (wasReset)
    report("RTC", T_WARN, "OK, but time was lost - check CR2032 (BT1)");
  else
    report("RTC", T_PASS, "ticking, NVRAM OK, time kept");
}

static void testInputsIdle() {
  Serial.println("\n[4] Sensor inputs, idle level (pull-ups -> expect HIGH if unplugged)");
  uint8_t lowCount = 0;
  for (uint8_t i = 0; i < 4; i++) {
    int v = digitalRead(PIN_FLOW[i]);
    Serial.printf("  Flow%u (IO%u) = %s\n", i + 1, PIN_FLOW[i], v ? "HIGH" : "LOW");
    if (!v) lowCount++;
  }
  int ps = digitalRead(PIN_PS);
  Serial.printf("  Pressure (IO%u) = %s\n", PIN_PS, ps ? "HIGH (switch open)" : "LOW (switch closed)");
  if (lowCount)
    report("Flow inputs", T_WARN, "%u input(s) LOW at idle - pull-ups not fitted yet, sensor parked low, or short", lowCount);
  else
    report("Flow inputs", T_PASS, "all 4 HIGH at idle");
  report("Pressure input", T_PASS, ps ? "open (HIGH)" : "closed (LOW)");
}

static void testWifi() {
  Serial.println("\n[5] WiFi radio scan");
  digitalWrite(PIN_LED_WIFI, HIGH);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  digitalWrite(PIN_LED_WIFI, LOW);
  if (n <= 0) {
    WiFi.mode(WIFI_OFF);
    report("WiFi", n == 0 ? T_WARN : T_FAIL, n == 0 ? "radio OK but no networks seen" : "scan failed");
    return;
  }
  int best = -127;
  for (int i = 0; i < n && i < 8; i++) {
    Serial.printf("  %-32s %4d dBm  ch%d\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
    if (WiFi.RSSI(i) > best) best = WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  report("WiFi", T_PASS, "%d networks, best %d dBm", n, best);
}

static void pulseValve(uint8_t v, bool forward, uint16_t ms) {
  uint8_t in1 = v == 1 ? PIN_V1_IN1 : PIN_V2_IN1;
  uint8_t in2 = v == 1 ? PIN_V1_IN2 : PIN_V2_IN2;
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(forward ? in1 : in2, HIGH);
  delay(ms);
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
}

static void testValves(bool interactive) {
  Serial.println("\n[6] Latch valve drivers (DRV8871 U1/U2)");
  lastBattV = readBatteryVolts();
  if (lastBattV < 8.0f) {
    report("Valve drivers", T_SKIP, "needs 12V on J6 (now %.1f V)", lastBattV);
    return;
  }
  const uint16_t ms = 250;  // long enough to see the direction LED
  for (uint8_t v = 1; v <= 2; v++) {
    Serial.printf("  V%u forward  %u ms -> watch one direction LED (and valve OPEN)\n", v, ms);
    pulseValve(v, true, ms);
    delay(600);
    Serial.printf("  V%u reverse  %u ms -> the other LED (and valve CLOSE)\n", v, ms);
    pulseValve(v, false, ms);
    delay(600);
  }
  float vAfter = readBatteryVolts();
  Serial.printf("  12V after pulses: %.2f V\n", vAfter);
  report("Valve drivers", interactive ? T_PASS : T_WARN,
         interactive ? "pulsed both ways - confirm LEDs/valves by eye"
                     : "pulsed both ways - confirm the 4 direction LEDs by eye");
}

static void ledWalk() {
  Serial.println("\n[LED] walk: Flow1 Flow2 Flow3 Flow4 PS WiFi, then all");
  const char* names[] = {"Flow1 LD3", "Flow2 LD4", "Flow3 LD5", "Flow4 LD6", "PS LD7", "WiFi LD2"};
  const uint8_t pins[] = {PIN_LED_FLOW[0], PIN_LED_FLOW[1], PIN_LED_FLOW[2], PIN_LED_FLOW[3],
                          PIN_LED_PS, PIN_LED_WIFI};
  for (uint8_t i = 0; i < 6; i++) {
    Serial.printf("  %s (IO%u)\n", names[i], pins[i]);
    digitalWrite(pins[i], HIGH);
    delay(400);
    digitalWrite(pins[i], LOW);
  }
  allLeds(true);
  delay(600);
  allLeds(false);
}

// Live view: counts pulses on each flow input, mirrors activity on its LED,
// and lights the PS LED while the pressure switch is closed.
static void monitorInputs() {
  Serial.println("\n[MON] Live inputs - spin a flow sensor / short a input to GND. Any key to stop.");
  for (uint8_t i = 0; i < 4; i++) {
    flowCount[i] = 0;
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW[i]), flowIsr[i], FALLING);
  }
  uint32_t last[4] = {0}, lastSec[4] = {0}, total[4] = {0};
  uint32_t ledUntil[4] = {0};
  uint32_t nextPrint = millis() + 1000;
  while (!Serial.available()) {
    uint32_t now = millis();
    for (uint8_t i = 0; i < 4; i++) {
      uint32_t c = flowCount[i];
      if (c != last[i]) { ledUntil[i] = now + 150; last[i] = c; }
      digitalWrite(PIN_LED_FLOW[i], now < ledUntil[i]);
    }
    digitalWrite(PIN_LED_PS, digitalRead(PIN_PS) == LOW);
    if (now >= nextPrint) {
      nextPrint += 1000;
      Serial.print("  Hz:");
      for (uint8_t i = 0; i < 4; i++) {
        uint32_t c = flowCount[i];
        total[i] = c;
        Serial.printf("  F%u=%4lu", i + 1, (unsigned long)(c - lastSec[i]));
        lastSec[i] = c;
      }
      Serial.printf("  | totals %lu/%lu/%lu/%lu | PS=%s | 12V=%.1f\n",
                    (unsigned long)total[0], (unsigned long)total[1],
                    (unsigned long)total[2], (unsigned long)total[3],
                    digitalRead(PIN_PS) ? "open" : "CLOSED", readBatteryVolts());
    }
    delay(10);
  }
  while (Serial.available()) Serial.read();
  for (uint8_t i = 0; i < 4; i++) detachInterrupt(digitalPinToInterrupt(PIN_FLOW[i]));
  allLeds(false);
  Serial.println("  monitor stopped");
}

static void printSummary() {
  uint8_t fails = 0, warns = 0;
  Serial.println("\n================ SELF-TEST SUMMARY ================");
  for (uint8_t i = 0; i < resultCount; i++) {
    Serial.printf("  %-15s %-4s  %s\n", results[i].name, statusText(results[i].status),
                  results[i].detail);
    if (results[i].status == T_FAIL) fails++;
    if (results[i].status == T_WARN) warns++;
  }
  Serial.printf("  => %s (%u fail, %u warn)\n", fails ? "FAIL" : (warns ? "PASS with warnings" : "PASS"),
                fails, warns);
  Serial.println("  Not auto-checkable: LEDs (run 'l'), sensors (run 'm').");
  Serial.println("===================================================");
}

static void runAll() {
  resultCount = 0;
  testChip();
  testBattery();
  testRtc();
  testInputsIdle();
  testWifi();
  testValves(false);
  ledWalk();
  printSummary();
}

static void printMenu() {
  Serial.println(
      "\nCommands:\n"
      "  a  run all tests + summary     l  LED walk\n"
      "  m  live sensor monitor         b  read 12V\n"
      "  r  read RTC                    s  set RTC to build time\n"
      "  w  WiFi scan                   v  valve test (both, both ways)\n"
      "  1  V1 forward   2  V1 reverse  3  V2 forward   4  V2 reverse   (250 ms)\n"
      "  h  this menu");
}

// ---------------------------------------------------------------- main

void setup() {
  allOutputsSafe();  // valves off before anything else
  for (uint8_t p : PIN_FLOW) pinMode(p, INPUT);  // board has the pull-ups; 34-39 have none inside
  pinMode(PIN_PS, INPUT);
  analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
  Wire.begin(PIN_SDA, PIN_SCL, 100000);

  Serial.begin(115200);
  delay(300);
  Serial.printf("\n\n=== %s  (built %s %s) ===\n", FW_NAME, __DATE__, __TIME__);
  runAll();
  printMenu();
}

void loop() {
  if (!Serial.available()) { delay(10); return; }
  char c = Serial.read();
  switch (c) {
    case 'a': runAll(); break;
    case 'l': ledWalk(); break;
    case 'm': monitorInputs(); break;
    case 'b': testBattery(); break;
    case 'r': testRtc(); break;
    case 's': Serial.println(rtcSetToBuildTime() ? "  RTC set" : "  RTC set FAILED"); rtcPrintNow(); break;
    case 'w': testWifi(); break;
    case 'v': testValves(true); break;
    case '1': Serial.println("  V1 forward"); pulseValve(1, true, 250); break;
    case '2': Serial.println("  V1 reverse"); pulseValve(1, false, 250); break;
    case '3': Serial.println("  V2 forward"); pulseValve(2, true, 250); break;
    case '4': Serial.println("  V2 reverse"); pulseValve(2, false, 250); break;
    case 'h': case '?': printMenu(); break;
    case '\r': case '\n': case ' ': break;
    default: Serial.printf("  unknown '%c' - h for help\n", c);
  }
}
