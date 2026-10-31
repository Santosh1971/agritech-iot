#pragma once
// Temperature / humidity sensor on the FG1 I2C bus (shared with the DS1307
// RTC at 0x68). Auto-detects the common parts so the same firmware works
// whichever sensor stick is fitted:
//   SHT4x  @0x44 / 0x45     SHT3x  @0x44 / 0x45
//   AHT20/AHT21 @0x38       HTU21D / SHT21 / Si7021 @0x40
// read() blocks for at most ~80 ms (AHT20 conversion); others ~10-20 ms.
#include <Arduino.h>
#include <Wire.h>
#include "EspNowProto.h"

class THSensor {
public:
    // Wire must already be started (RTCManager::begin() does it on the
    // controller; the node calls Wire.begin itself).
    bool begin();
    bool read();                 // true on a good reading
    float t()  const { return _t; }
    float rh() const { return _rh; }
    bool  ok() const { return _ok; }
    SensorKind kind() const { return _kind; }
    uint8_t address() const { return _addr; }
    const char* name() const;
    uint32_t errors() const { return _errors; }

private:
    bool _probeSht4x(uint8_t a);
    bool _probeSht3x(uint8_t a);
    bool _probeAht(uint8_t a);
    bool _probeHtu(uint8_t a);
    bool _readSht4x();
    bool _readSht3x();
    bool _readAht();
    bool _readHtu();
    bool _present(uint8_t a);
    static uint8_t _crc8(const uint8_t* d, uint8_t n);

    SensorKind _kind = SENSOR_NONE;
    uint8_t _addr = 0;
    float _t = 0, _rh = 0;
    bool  _ok = false;
    uint8_t  _fails = 0;
    uint32_t _errors = 0;
    uint32_t _lastProbeMs = 0;
};
