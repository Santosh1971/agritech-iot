#include "hw.h"

#include <Preferences.h>
#include <driver/pcnt.h>
#include <Wire.h>

#include "settings.h"
#include "timeutil.h"
#include "wm2_pins.h"

using namespace wm2;

namespace hw {

// ======================================================================= RTC

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static bool rtcReadRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)RTC_I2C_ADDR, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool rtcBegin() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  uint8_t b;
  return rtcReadRegs(0, &b, 1);
}

bool rtcRead(uint32_t& utc) {
  uint8_t t[7];
  if (!rtcReadRegs(0, t, 7)) return false;
  if (t[0] & 0x80) return false;  // clock halted
  int year = 2000 + bcd2dec(t[6]);
  unsigned mon = bcd2dec(t[5] & 0x1F), day = bcd2dec(t[4] & 0x3F);
  if (year < 2025 || mon < 1 || mon > 12 || day < 1 || day > 31) return false;
  int32_t days = daysFromCivil(year, mon, day);
  utc = (uint32_t)days * 86400u + bcd2dec(t[2] & 0x3F) * 3600u + bcd2dec(t[1] & 0x7F) * 60u +
        bcd2dec(t[0] & 0x7F);
  return true;
}

bool rtcWrite(uint32_t utc) {
  int y;
  unsigned m, d;
  civilFromDays((int32_t)(utc / 86400u), y, m, d);
  uint32_t s = utc % 86400u;
  uint8_t t[7] = {dec2bcd(s % 60), dec2bcd((s / 60) % 60), dec2bcd(s / 3600), 1,
                  dec2bcd(d), dec2bcd(m), dec2bcd(y % 100)};
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(0);
  Wire.write(t, 7);
  return Wire.endTransmission() == 0;
}

// ==================================================================== Valves

static Preferences prefs;
static bool vState[2] = {false, false};
static bool vWant[2] = {false, false};
static bool vPending[2] = {false, false};
static uint32_t lastPulseEnd = 0;
static uint32_t nextRetry = 0;

static void drvPins(int v, uint8_t& in1, uint8_t& in2) {
  in1 = v == 0 ? PIN_V1_IN1 : PIN_V2_IN1;
  in2 = v == 0 ? PIN_V1_IN2 : PIN_V2_IN2;
}

void valvesBegin() {
  const uint8_t pins[] = {PIN_V1_IN1, PIN_V1_IN2, PIN_V2_IN1, PIN_V2_IN2};
  for (uint8_t p : pins) {
    digitalWrite(p, LOW);
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  prefs.begin("valves", false);
  vState[0] = vWant[0] = prefs.getBool("v0", false);
  vState[1] = vWant[1] = prefs.getBool("v1", false);
}

void valvePulseRaw(int v, bool open) {
  uint8_t in1, in2;
  drvPins(v, in1, in2);
  bool forward = open != cfg.invert[v];
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(forward ? in1 : in2, HIGH);
  delay(cfg.pulseMs[v]);
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  lastPulseEnd = millis();
}

void valveRequest(int v, bool open, bool force) {
  if (v < 0 || v > 1) return;
  vWant[v] = open;
  if (force || open != vState[v]) vPending[v] = true;
}

void valveResync() {
  vPending[0] = vPending[1] = true;
}

void valvesLoop() {
  uint32_t now = millis();
  if (now - lastPulseEnd < (uint32_t)(cfg.gapS * 1000)) return;
  for (int v = 0; v < 2; v++) {
    if (!vPending[v]) continue;
    // A latching valve needs the 12V rail; on USB power the pulse waits.
    if (battVolts() < 9.0f) {
      if ((int32_t)(now - nextRetry) < 0) return;
      nextRetry = now + 10000;
      return;
    }
    valvePulseRaw(v, vWant[v]);
    vState[v] = vWant[v];
    vPending[v] = false;
    prefs.putBool(v == 0 ? "v0" : "v1", vState[v]);
    Serial.printf("[valve] V%d %s\n", v + 1, vState[v] ? "OPEN pulse" : "CLOSE pulse");
    return;  // one pulse per gap
  }
}

bool valveState(int v) { return vState[v]; }
bool valvePending(int v) { return vPending[v]; }

// ====================================================================== Flow

// Counted by the ESP32 pulse counter (PCNT), not interrupts: GPIO36/39 get
// ~80 ns false pulses while the ADC or WiFi power save is active (ESP32
// errata 3.11). The PCNT glitch filter (12.5 us) rejects them.
static const pcnt_unit_t PCNT_UNIT[4] = {PCNT_UNIT_0, PCNT_UNIT_1, PCNT_UNIT_2, PCNT_UNIT_3};
static uint32_t fCount[4] = {0, 0, 0, 0};   // running totals (pulses)
static uint32_t fLast[4] = {0, 0, 0, 0};
static uint32_t fHist[4][5] = {};
static uint8_t fHistIdx = 0;
static float fTotal[4] = {0, 0, 0, 0};
static uint32_t fLastLedPulse[4] = {0, 0, 0, 0};
static int16_t fLedSeen[4] = {0, 0, 0, 0};

void flowBegin() {
  for (int i = 0; i < 4; i++) {
    pinMode(PIN_FLOW[i], INPUT);  // board pull-ups
    pcnt_config_t c = {};
    c.pulse_gpio_num = PIN_FLOW[i];
    c.ctrl_gpio_num = PCNT_PIN_NOT_USED;
    c.channel = PCNT_CHANNEL_0;
    c.unit = PCNT_UNIT[i];
    c.pos_mode = PCNT_COUNT_DIS;
    c.neg_mode = PCNT_COUNT_INC;  // count falling edges
    c.lctrl_mode = PCNT_MODE_KEEP;
    c.hctrl_mode = PCNT_MODE_KEEP;
    c.counter_h_lim = 32000;
    c.counter_l_lim = 0;
    pcnt_unit_config(&c);
    pcnt_set_filter_value(PCNT_UNIT[i], 1000);  // 1000 APB cycles = 12.5 us
    pcnt_filter_enable(PCNT_UNIT[i]);
    pcnt_counter_pause(PCNT_UNIT[i]);
    pcnt_counter_clear(PCNT_UNIT[i]);
    pcnt_counter_resume(PCNT_UNIT[i]);
  }
}

// Move counts from the hardware counters into the running totals.
static void flowCollect() {
  for (int i = 0; i < 4; i++) {
    int16_t v = 0;
    pcnt_get_counter_value(PCNT_UNIT[i], &v);
    if (v > 0) {
      pcnt_counter_clear(PCNT_UNIT[i]);
      fCount[i] += (uint32_t)v;
    }
    fLedSeen[i] = 0;
  }
}

void flowSample(float lpm[4], float litresDelta[4]) {
  flowCollect();
  for (int i = 0; i < 4; i++) {
    uint32_t c = fCount[i];
    uint32_t d = c - fLast[i];
    fLast[i] = c;
    fHist[i][fHistIdx] = d;
    float ppl = cfg.ppl[i] > 0 ? cfg.ppl[i] : 450;
    litresDelta[i] = d / ppl;
    fTotal[i] += litresDelta[i];
    uint32_t sum = 0;
    for (int k = 0; k < 5; k++) sum += fHist[i][k];
    lpm[i] = (sum / 5.0f) / ppl * 60.0f;  // pulses per second averaged over 5 s
  }
  fHistIdx = (fHistIdx + 1) % 5;
}

float flowTotal(int i) { return fTotal[i]; }
uint32_t flowPulses(int i) {
  int16_t v = 0;
  pcnt_get_counter_value(PCNT_UNIT[i], &v);
  return fCount[i] + (v > 0 ? v : 0);
}

// ================================================================== Pressure

static bool psStable = false;
static bool psLastRaw = false;
static uint32_t psChangedAt = 0;

void psBegin() {
  pinMode(PIN_PS, INPUT);
  psLastRaw = psStable = digitalRead(PIN_PS) == LOW;
}

bool psRaw() { return digitalRead(PIN_PS) == LOW; }

bool psState() {
  bool r = psRaw();
  uint32_t now = millis();
  if (r != psLastRaw) {
    psLastRaw = r;
    psChangedAt = now;
  } else if (r != psStable && now - psChangedAt >= (uint32_t)(cfg.psSettleS * 1000)) {
    psStable = r;
  }
  return psStable;
}

// =================================================================== Battery

static float bV = 0;
static float bTrendRef = 0;
static uint32_t bTrendAt = 0;
static const char* bTrend = "steady";

float battRead() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(PIN_BATT_ADC);
  float v = (mv / 16.0f) * BATT_DIVIDER / 1000.0f;
  if (v < 1.5f) v = 0;  // ADC floor (~0.14 V at the pin) reads ~0.9 V with no 12V
  bV = bV == 0 ? v : bV * 0.8f + v * 0.2f;
  if (v == 0) bV = 0;
  uint32_t now = millis();
  if (bTrendAt == 0) { bTrendRef = bV; bTrendAt = now; }
  if (now - bTrendAt > 600000UL) {  // compare with 10 minutes ago
    float d = bV - bTrendRef;
    bTrend = d > 0.05f ? "charging" : d < -0.05f ? "discharging" : "steady";
    bTrendRef = bV;
    bTrendAt = now;
  }
  return bV;
}

float battVolts() { return bV; }

int battSoc() {
  if (bV <= 0) return 0;
  // Resting-voltage tables (12 V packs).
  static const float lfpV[] = {10.0f, 12.0f, 12.5f, 12.8f, 12.9f, 13.0f, 13.1f, 13.2f, 13.3f, 13.4f, 13.6f};
  static const int lfpS[] = {0, 9, 14, 17, 20, 30, 40, 70, 90, 99, 100};
  static const float laV[] = {11.6f, 11.8f, 12.0f, 12.2f, 12.4f, 12.6f, 12.8f};
  static const int laS[] = {0, 20, 40, 60, 80, 95, 100};
  const float* tv = cfg.chem == 1 ? laV : lfpV;
  const int* ts = cfg.chem == 1 ? laS : lfpS;
  int n = cfg.chem == 1 ? 7 : 11;
  if (bV <= tv[0]) return 0;
  if (bV >= tv[n - 1]) return 100;
  for (int i = 1; i < n; i++)
    if (bV < tv[i]) return ts[i - 1] + (int)((bV - tv[i - 1]) / (tv[i] - tv[i - 1]) * (ts[i] - ts[i - 1]));
  return 100;
}

const char* battTrend() { return bV <= 0 ? "none" : bTrend; }

const char* battLevel() {
  if (bV <= 0) return "none";
  if (bV < cfg.vCrit) return "critical";
  if (bV < cfg.vSave) return "save";
  if (bV < cfg.vWarn) return "warning";
  return "ok";
}

float battDaysLeft() {
  if (bV <= 0 || cfg.dailyAh <= 0) return 0;
  return battSoc() / 100.0f * cfg.capAh * 0.9f / cfg.dailyAh;
}

// ====================================================================== LEDs

void ledsBegin() {
  for (uint8_t p : PIN_LED_FLOW) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  pinMode(PIN_LED_PS, OUTPUT);
  pinMode(PIN_LED_WIFI, OUTPUT);
}

void ledsLoop(WifiLed w, bool water, bool bypass) {
  uint32_t now = millis();
  // Flow: flash on activity (raw pulses are too fast to see).
  for (int i = 0; i < 4; i++) {
    int16_t v = 0;
    pcnt_get_counter_value(PCNT_UNIT[i], &v);  // pulses since last collect
    if (v != fLedSeen[i]) { fLedSeen[i] = v; fLastLedPulse[i] = now; }
    digitalWrite(PIN_LED_FLOW[i], now - fLastLedPulse[i] < 150);
  }
  // Pressure: on = water, blinking = bypass on.
  digitalWrite(PIN_LED_PS, bypass ? (now / 500) % 2 : water);
  // WiFi: fast blink = SoftAP, 1 blink / 3 s = MQTT ok, 2 blinks / 3 s = WiFi only.
  uint32_t ph = now % 3000;
  bool on = false;
  switch (w) {
    case WL_AP: on = (now / 250) % 2; break;
    case WL_MQTT: on = ph < 80; break;
    case WL_WIFI: on = ph < 80 || (ph > 250 && ph < 330); break;
    default: on = false;
  }
  digitalWrite(PIN_LED_WIFI, on);
}

}  // namespace hw
