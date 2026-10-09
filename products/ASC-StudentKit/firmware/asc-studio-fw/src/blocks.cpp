#include "blocks.h"

#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <BH1750.h>
#include <DHTesp.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Wire.h>

namespace {

// Student I2C ports share one bus (Wire1); the RTC has its own (Wire, main.cpp).
TwoWire& extBus = Wire1;
bool extBusStarted = false;

// The tank level sensor (A02YYUW) talks on UART1, receive only, so a design
// can have one of them (design.cpp refuses a second).
HardwareSerial& levelUart = Serial1;

// Tipping-bucket rain gauge: millimetres of rain per tip of the bucket
// (the common Misol / WH-SP-RG gauge). Readings are the rain of the last 24 h.
const float MM_PER_TIP = 0.2794f;

// One driver object per slot, created when a design is loaded.
struct Driver {
  OneWire* ow = nullptr;
  DallasTemperature* dallas = nullptr;
  DHTesp* dht = nullptr;
  Adafruit_BME280* bme = nullptr;
  BH1750* light = nullptr;
  bool found = false;
  int8_t irqPin = -1;
  volatile uint32_t pulses = 0;
  uint32_t lastPulses = 0;
  uint32_t lastPulseMs = 0;
  uint32_t lastDhtMs = 0;
  // rain gauge: debounce, and tips counted per hour for the last 24 hours
  volatile uint32_t lastTipMs = 0;
  uint16_t hourTips[24] = {};
  uint8_t hour = 0;
  uint32_t hourStartMs = 0;
  // tank level: the last frame from the sensor
  bool uart = false;
  uint8_t frame[4] = {};
  uint32_t lastFrameMs = 0;
  // I2C sensors that measure on their own clock: when the last good reading came
  uint32_t lastOkMs = 0;
};

// A sensor that measures on its own clock may have nothing new at the moment
// it is asked; keep its last reading this long before calling it failed, so
// outputs don't drop out (fail-safe OFF) between readings.
const uint32_t STALE_MS = 15000;
Driver drivers[MAX_SLOTS];

void IRAM_ATTR onPulse(void* arg) { (*(volatile uint32_t*)arg)++; }

// A reed switch bounces: count a tip only 100 ms after the last one.
void IRAM_ATTR onTip(void* arg) {
  Driver* dr = (Driver*)arg;
  uint32_t now = millis();
  if (now - dr->lastTipMs > 100) { dr->pulses++; dr->lastTipMs = now; }
}

// Sensirion I2C framing (SHT3x, SCD4x): 16-bit commands, each 16-bit word read
// back followed by a CRC-8 (polynomial 0x31, init 0xFF).
uint8_t crc8(const uint8_t* d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
  }
  return c;
}

bool i2cCmd(uint8_t addr, uint16_t cmd) {
  extBus.beginTransmission(addr);
  extBus.write((uint8_t)(cmd >> 8));
  extBus.write((uint8_t)(cmd & 0xFF));
  return extBus.endTransmission() == 0;
}

bool i2cWords(uint8_t addr, uint16_t* w, int n) {
  if (extBus.requestFrom(addr, (uint8_t)(n * 3)) != n * 3) return false;
  for (int i = 0; i < n; i++) {
    uint8_t b[3];
    for (int k = 0; k < 3; k++) b[k] = extBus.read();
    if (crc8(b, 2) != b[2]) return false;
    w[i] = (uint16_t)(b[0] << 8 | b[1]);
  }
  return true;
}

// MLX90614 object temperature (RAM 0x07): 0.02 K per count. NAN if no answer.
float mlxObjectC() {
  extBus.beginTransmission(0x5A);
  extBus.write(0x07);
  if (extBus.endTransmission(false) != 0) return NAN;
  if (extBus.requestFrom((uint8_t)0x5A, (uint8_t)3) != 3) return NAN;
  uint16_t raw = extBus.read();
  raw |= extBus.read() << 8;
  extBus.read();  // PEC
  if (raw & 0x8000) return NAN;  // error flag
  return raw * 0.02f - 273.15f;
}

void inputPin(int8_t gpio, uint8_t mode) {
#if CONFIG_IDF_TARGET_ESP32
  // GPIO34-39 on a classic ESP32 are input only, with no pull-up or pull-down.
  if (gpio >= 34) mode = INPUT;
#endif
  pinMode(gpio, mode);
}

void freeDriver(Driver& dr) {
  if (dr.irqPin >= 0) detachInterrupt(dr.irqPin);
  if (dr.uart) levelUart.end();
  delete dr.dallas; delete dr.ow; delete dr.dht; delete dr.bme; delete dr.light;
  dr = Driver();
}

bool is(const Slot& s, const char* id) { return strcmp(s.block, id) == 0; }
bool isDallas(const Slot& s) { return is(s, "soilt") || is(s, "probet"); }

float avgMv(int8_t gpio) {
  uint32_t sum = 0;
  for (int k = 0; k < 8; k++) sum += analogReadMilliVolts(gpio);
  return sum / 8.0f;
}

void setKeys(Slot& s, std::initializer_list<const char*> keys) {
  s.nValues = 0;
  for (const char* k : keys) if (s.nValues < MAX_VALUES) s.valueKeys[s.nValues++] = k;
}

float soilPercent(const Slot& s, float mv) {
  if (s.dryMv <= s.wetMv) return NAN;
  float p = (s.dryMv - mv) * 100.0f / (s.dryMv - s.wetMv);
  return p < 0 ? 0 : p > 100 ? 100 : p;
}

}  // namespace

void blocksBegin(Design& d, const BoardMap& board) {
  for (int i = 0; i < MAX_SLOTS; i++) freeDriver(drivers[i]);

  for (int i = 0; i < d.nSlots; i++) {
    Slot& s = d.slots[i];
    Driver& dr = drivers[i];
    s.ok = false;
    if (s.kind == KIND_I2C && !extBusStarted) {
      extBus.begin(board.i2cExtSda, board.i2cExtScl);
      extBusStarted = true;
    }
    if (is(s, "soil") || is(s, "tds")) {
      setKeys(s, {""});
      analogSetPinAttenuation(s.gpio, ADC_11db);
      pinMode(s.gpio, INPUT);
    } else if (isDallas(s)) {
      setKeys(s, {""});
      dr.ow = new OneWire(s.gpio);
      dr.dallas = new DallasTemperature(dr.ow);
      dr.dallas->begin();
      dr.dallas->setWaitForConversion(false);
      dr.dallas->requestTemperatures();
      dr.found = dr.dallas->getDeviceCount() > 0;
    } else if (is(s, "float") || is(s, "rain") || is(s, "door")) {
      setKeys(s, {""});
      pinMode(s.gpio, INPUT_PULLUP);
    } else if (is(s, "pir")) {
      setKeys(s, {""});
      inputPin(s.gpio, INPUT_PULLDOWN);  // HC-SR501 drives its output high on motion
    } else if (is(s, "raing")) {
      setKeys(s, {""});
      inputPin(s.gpio, INPUT_PULLUP);
      attachInterruptArg(s.gpio, onTip, (void*)&dr, FALLING);
      dr.irqPin = s.gpio;
      dr.hourStartMs = millis();
    } else if (is(s, "level")) {
      setKeys(s, {""});
      levelUart.begin(9600, SERIAL_8N1, s.gpio, -1);  // receive only
      dr.uart = true;
    } else if (is(s, "flow")) {
      setKeys(s, {""});
#if CONFIG_IDF_TARGET_ESP32
      // GPIO34-39 on a classic ESP32 are input only, with no pull-up; the
      // flow sensor's own pull-up holds the line.
      pinMode(s.gpio, s.gpio >= 34 ? INPUT : INPUT_PULLUP);
#else
      pinMode(s.gpio, INPUT_PULLUP);
#endif
      attachInterruptArg(s.gpio, onPulse, (void*)&dr.pulses, FALLING);
      dr.irqPin = s.gpio;
      dr.lastPulseMs = millis();
    } else if (is(s, "dht")) {
      setKeys(s, {"t", "h"});
      dr.dht = new DHTesp();
      dr.dht->setup(s.gpio, DHTesp::DHT22);
    } else if (is(s, "bme")) {
      setKeys(s, {"t", "h", "p"});
      dr.bme = new Adafruit_BME280();
      dr.found = dr.bme->begin(0x76, &extBus) || dr.bme->begin(0x77, &extBus);
    } else if (is(s, "light")) {
      setKeys(s, {""});
      dr.light = new BH1750(0x23);
      dr.found = dr.light->begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &extBus);
    } else if (is(s, "sht")) {
      setKeys(s, {"t", "h"});
      i2cCmd(0x44, 0x30A2);  // soft reset
      delay(2);
      dr.found = i2cCmd(0x44, 0x2236);  // periodic, 2 readings a second, high repeatability
    } else if (is(s, "co2")) {
      setKeys(s, {"c", "t", "h"});
      i2cCmd(0x62, 0x3F86);  // stop a measurement left running by an earlier design
      delay(500);
      dr.found = i2cCmd(0x62, 0x21B1);  // periodic, one reading every 5 s
    } else if (is(s, "irtemp")) {
      setKeys(s, {""});
      dr.found = !isnan(mlxObjectC());
    } else {
      s.nValues = 0;  // outputs: main.cpp drives the pin
    }
  }
}

void blocksRead(Design& d) {
  uint32_t now = millis();
  for (int i = 0; i < d.nSlots; i++) {
    Slot& s = d.slots[i];
    Driver& dr = drivers[i];
    if (is(s, "soil")) {
      float mv = avgMv(s.gpio);
      // A floating pin with nothing plugged in reads near 0 mV.
      s.ok = mv > 150;
      s.values[0] = s.ok ? soilPercent(s, mv) : NAN;
    } else if (is(s, "tds")) {
      // Gravity-type TDS meter: probe voltage to ppm at 25 °C (DFRobot's curve).
      float v = avgMv(s.gpio) / 1000.0f;
      s.ok = true;
      s.values[0] = (133.42f * v * v * v - 255.86f * v * v + 857.39f * v) * 0.5f;
    } else if (isDallas(s) && dr.dallas) {
      float c = dr.dallas->getTempCByIndex(0);
      s.ok = c != DEVICE_DISCONNECTED_C && c > -55 && c < 125;
      s.values[0] = s.ok ? c : NAN;
      dr.dallas->requestTemperatures();  // ready for the next read
    } else if (is(s, "float")) {
      s.ok = true;
      s.values[0] = digitalRead(s.gpio) == LOW ? 1 : 0;  // closed to GND = water present
    } else if (is(s, "rain")) {
      s.ok = true;
      s.values[0] = digitalRead(s.gpio) == LOW ? 1 : 0;  // module pulls low when wet
    } else if (is(s, "door")) {
      s.ok = true;
      s.values[0] = digitalRead(s.gpio) == HIGH ? 1 : 0;  // magnet away, contact open = door open
    } else if (is(s, "pir")) {
      s.ok = true;
      s.values[0] = digitalRead(s.gpio) == HIGH ? 1 : 0;
    } else if (is(s, "raing")) {
      while (now - dr.hourStartMs >= 3600000UL) {  // a new hour: forget the oldest
        dr.hour = (dr.hour + 1) % 24;
        dr.hourTips[dr.hour] = 0;
        dr.hourStartMs += 3600000UL;
      }
      uint32_t p = dr.pulses;
      dr.hourTips[dr.hour] += p - dr.lastPulses;
      dr.lastPulses = p;
      uint32_t tips = 0;
      for (uint16_t t : dr.hourTips) tips += t;
      s.ok = true;
      s.values[0] = tips * MM_PER_TIP;
    } else if (is(s, "level")) {
      // A02YYUW frame: 0xFF, distance high byte, low byte (mm), checksum.
      while (levelUart.available()) {
        memmove(dr.frame, dr.frame + 1, 3);
        dr.frame[3] = levelUart.read();
        if (dr.frame[0] == 0xFF && (uint8_t)(dr.frame[0] + dr.frame[1] + dr.frame[2]) == dr.frame[3]) {
          s.values[0] = (dr.frame[1] << 8 | dr.frame[2]) / 10.0f;
          dr.lastFrameMs = now;
        }
      }
      s.ok = dr.lastFrameMs && now - dr.lastFrameMs < 3000;
      if (!s.ok) s.values[0] = NAN;
    } else if (is(s, "flow")) {
      uint32_t dt = now - dr.lastPulseMs;
      if (dt >= 1000) {
        uint32_t p = dr.pulses;
        // YF-S201 style sensor: pulse frequency (Hz) = 7.5 x flow (L/min)
        s.values[0] = (p - dr.lastPulses) * 1000.0f / dt / 7.5f;
        dr.lastPulses = p;
        dr.lastPulseMs = now;
        s.ok = true;
      }
    } else if (is(s, "dht") && dr.dht) {
      if (now - dr.lastDhtMs >= 2000) {
        dr.lastDhtMs = now;
        TempAndHumidity v = dr.dht->getTempAndHumidity();
        s.ok = dr.dht->getStatus() == DHTesp::ERROR_NONE && !isnan(v.temperature);
        s.values[0] = s.ok ? v.temperature : NAN;
        s.values[1] = s.ok ? v.humidity : NAN;
      }
    } else if (is(s, "bme") && dr.bme && dr.found) {
      s.values[0] = dr.bme->readTemperature();
      s.values[1] = dr.bme->readHumidity();
      s.values[2] = dr.bme->readPressure() / 100.0f;
      s.ok = !isnan(s.values[0]);
    } else if (is(s, "light") && dr.light && dr.found) {
      float lx = dr.light->readLightLevel();
      s.ok = lx >= 0;
      s.values[0] = s.ok ? lx : NAN;
    } else if (is(s, "sht") && dr.found) {
      uint16_t w[2];
      if (i2cCmd(0x44, 0xE000) && i2cWords(0x44, w, 2)) {  // fetch the latest reading
        s.values[0] = -45 + 175.0f * w[0] / 65535;
        s.values[1] = 100.0f * w[1] / 65535;
        dr.lastOkMs = now;
      }
      s.ok = dr.lastOkMs && now - dr.lastOkMs < STALE_MS;
      if (!s.ok) s.values[0] = s.values[1] = NAN;
    } else if (is(s, "co2") && dr.found) {
      uint16_t w[3];
      bool ready = i2cCmd(0x62, 0xE4B8) && (delay(1), i2cWords(0x62, w, 1)) && (w[0] & 0x07FF);
      if (ready && i2cCmd(0x62, 0xEC05) && (delay(1), i2cWords(0x62, w, 3))) {
        s.values[0] = w[0];
        s.values[1] = -45 + 175.0f * w[1] / 65535;
        s.values[2] = 100.0f * w[2] / 65535;
        dr.lastOkMs = now;
      }  // else keep the last reading: a new one comes every 5 s
      s.ok = dr.lastOkMs && now - dr.lastOkMs < STALE_MS;
      if (!s.ok) s.values[0] = s.values[1] = s.values[2] = NAN;
    } else if (is(s, "irtemp") && dr.found) {
      float c = mlxObjectC();
      s.ok = !isnan(c);
      s.values[0] = c;
    }
  }
}

void blocksSelfTest(Design& d, JsonArray results) {
  blocksRead(d);
  for (int i = 0; i < d.nSlots; i++) {
    Slot& s = d.slots[i];
    Driver& dr = drivers[i];
    JsonObject r = results.add<JsonObject>();
    r["port"] = s.port;
    r["block"] = s.block;
    bool ok = true;
    String detail;
    if (is(s, "soil")) {
      ok = s.ok;
      detail = ok ? String("reading ") + String(s.values[0], 0) + " %" : "No probe found. Check the cable is in " + String(s.port) + ".";
    } else if (is(s, "tds")) {
      detail = String("reading ") + String(s.values[0], 0) + " ppm. Dip the probe in water to test it.";
    } else if (isDallas(s)) {
      ok = dr.dallas && dr.dallas->getDeviceCount() > 0;
      detail = ok ? "Probe found." : "No DS18B20 probe found on " + String(s.port) + ".";
    } else if (is(s, "dht")) {
      ok = s.ok;
      detail = ok ? "Sensor answers." : "No answer from the DHT22 yet. Wait 2 seconds and test again.";
    } else if (is(s, "level")) {
      ok = s.ok;
      detail = ok ? String("reading ") + String(s.values[0], 0) + " cm" : "No reading from the level sensor on " + String(s.port) + ". Check the cable, and point it at water or a wall less than 4.5 m away.";
    } else if (is(s, "co2")) {
      ok = dr.found;
      detail = ok ? "Found on the I2C bus. The first reading takes 5 seconds." : "Not found on " + String(s.port) + ". Check the 4-pin cable.";
    } else if (s.kind == KIND_I2C) {
      ok = dr.found;
      detail = ok ? "Found on the I2C bus." : "Not found on " + String(s.port) + ". Check the 4-pin cable.";
    } else if (s.kind == KIND_OUT) {
      detail = "Switch it from the Test page and listen for the click.";
    } else {
      detail = "Input ready.";
    }
    r["ok"] = ok;
    r["detail"] = detail;
  }
}

float valueFor(Design& d, const char* ref) {
  char port[8];
  strncpy(port, ref, sizeof(port) - 1);
  port[sizeof(port) - 1] = 0;
  const char* key = "";
  if (char* colon = strchr(port, ':')) { *colon = 0; key = strchr(ref, ':') + 1; }
  Slot* s = findSlot(d, port);
  if (!s || !s->ok) return NAN;
  for (int i = 0; i < s->nValues; i++) if (strcmp(s->valueKeys[i], key) == 0) return s->values[i];
  return NAN;
}

int guardOk(Design& d, const char* port) {
  Slot* s = findSlot(d, port);
  if (!s || !s->ok || isnan(s->values[0])) return -1;
  if (is(*s, "float")) return s->values[0] == 1 ? 1 : 0;
  if (is(*s, "rain")) return s->values[0] == 1 ? 0 : 1;
  return -1;
}
