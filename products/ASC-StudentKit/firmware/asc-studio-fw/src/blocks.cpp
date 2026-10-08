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
};
Driver drivers[MAX_SLOTS];

void IRAM_ATTR onPulse(void* arg) { (*(volatile uint32_t*)arg)++; }

void freeDriver(Driver& dr) {
  if (dr.irqPin >= 0) detachInterrupt(dr.irqPin);
  delete dr.dallas; delete dr.ow; delete dr.dht; delete dr.bme; delete dr.light;
  dr = Driver();
}

bool is(const Slot& s, const char* id) { return strcmp(s.block, id) == 0; }

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
    if (is(s, "soil")) {
      setKeys(s, {""});
      analogSetPinAttenuation(s.gpio, ADC_11db);
      pinMode(s.gpio, INPUT);
    } else if (is(s, "soilt")) {
      setKeys(s, {""});
      dr.ow = new OneWire(s.gpio);
      dr.dallas = new DallasTemperature(dr.ow);
      dr.dallas->begin();
      dr.dallas->setWaitForConversion(false);
      dr.dallas->requestTemperatures();
      dr.found = dr.dallas->getDeviceCount() > 0;
    } else if (is(s, "float") || is(s, "rain")) {
      setKeys(s, {""});
      pinMode(s.gpio, INPUT_PULLUP);
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
      uint32_t sum = 0;
      for (int k = 0; k < 8; k++) sum += analogReadMilliVolts(s.gpio);
      float mv = sum / 8.0f;
      // A floating pin with nothing plugged in reads near 0 mV.
      s.ok = mv > 150;
      s.values[0] = s.ok ? soilPercent(s, mv) : NAN;
    } else if (is(s, "soilt") && dr.dallas) {
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
    } else if (is(s, "soilt")) {
      ok = dr.dallas && dr.dallas->getDeviceCount() > 0;
      detail = ok ? "Probe found." : "No DS18B20 probe found on " + String(s.port) + ".";
    } else if (is(s, "dht")) {
      ok = s.ok;
      detail = ok ? "Sensor answers." : "No answer from the DHT22 yet. Wait 2 seconds and test again.";
    } else if (is(s, "bme") || is(s, "light")) {
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
