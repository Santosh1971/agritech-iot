#include "THSensor.h"

static const uint32_t REPROBE_MS = 30000;   // retry detection if nothing found / sensor lost

const char* THSensor::name() const {
    switch (_kind) {
        case SENSOR_SHT4X: return "SHT4x";
        case SENSOR_SHT3X: return "SHT3x";
        case SENSOR_AHT2X: return "AHT2x";
        case SENSOR_HTU21: return "HTU21/SHT21";
        default:           return "none";
    }
}

uint8_t THSensor::_crc8(const uint8_t* d, uint8_t n) {
    uint8_t crc = 0xFF;                      // Sensirion CRC-8, poly 0x31
    for (uint8_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
    return crc;
}

bool THSensor::_present(uint8_t a) {
    Wire.beginTransmission(a);
    return Wire.endTransmission() == 0;
}

bool THSensor::begin() {
    _lastProbeMs = millis();
    _kind = SENSOR_NONE;
    const uint8_t shtAddrs[] = {0x44, 0x45};
    for (uint8_t a : shtAddrs) {
        if (!_present(a)) continue;
        if (_probeSht4x(a)) { _kind = SENSOR_SHT4X; _addr = a; break; }
        if (_probeSht3x(a)) { _kind = SENSOR_SHT3X; _addr = a; break; }
    }
    if (_kind == SENSOR_NONE && _present(0x38) && _probeAht(0x38)) { _kind = SENSOR_AHT2X; _addr = 0x38; }
    if (_kind == SENSOR_NONE && _present(0x40) && _probeHtu(0x40)) { _kind = SENSOR_HTU21; _addr = 0x40; }

    if (_kind == SENSOR_NONE) {
        Serial.print("[TH] No T/RH sensor found. I2C devices:");
        for (uint8_t a = 1; a < 127; a++) if (_present(a)) Serial.printf(" 0x%02X", a);
        Serial.println();
        return false;
    }
    Serial.printf("[TH] %s at 0x%02X\n", name(), _addr);
    return read();
}

bool THSensor::read() {
    if (_kind == SENSOR_NONE) {
        _ok = false;
        if (millis() - _lastProbeMs >= REPROBE_MS) begin();
        return _ok;
    }
    bool good = false;
    switch (_kind) {
        case SENSOR_SHT4X: good = _readSht4x(); break;
        case SENSOR_SHT3X: good = _readSht3x(); break;
        case SENSOR_AHT2X: good = _readAht();   break;
        case SENSOR_HTU21: good = _readHtu();   break;
        default: break;
    }
    // Plausibility: a cable fault often shows up as -45 C / 0 % or 130 C.
    if (good && (_t < -20 || _t > 80 || _rh < 0 || _rh > 100.5f)) good = false;
    if (good) {
        if (_rh > 100) _rh = 100;
        _ok = true;
        _fails = 0;
    } else {
        _errors++;
        // One bad read keeps the last value; three in a row = fault.
        if (++_fails >= 3) {
            _ok = false;
            if (_fails >= 6) { _kind = SENSOR_NONE; _lastProbeMs = millis(); }  // re-detect
        }
    }
    return good;
}

// ---------------- SHT4x ----------------
bool THSensor::_probeSht4x(uint8_t a) {
    _addr = a;
    Wire.beginTransmission(a); Wire.write(0x94); Wire.endTransmission();  // soft reset
    delay(2);
    return _readSht4x();
}
bool THSensor::_readSht4x() {
    Wire.beginTransmission(_addr);
    Wire.write(0xFD);                                       // high precision
    if (Wire.endTransmission() != 0) return false;
    delay(10);
    uint8_t d[6];
    if (Wire.requestFrom(_addr, (uint8_t)6) != 6) return false;
    for (auto& b : d) b = Wire.read();
    if (_crc8(d, 2) != d[2] || _crc8(d + 3, 2) != d[5]) return false;
    uint16_t rt = (d[0] << 8) | d[1], rr = (d[3] << 8) | d[4];
    _t  = -45 + 175.0f * rt / 65535.0f;
    _rh = -6 + 125.0f * rr / 65535.0f;
    if (_rh < 0) _rh = 0;
    return true;
}

// ---------------- SHT3x ----------------
bool THSensor::_probeSht3x(uint8_t a) {
    _addr = a;
    return _readSht3x();
}
bool THSensor::_readSht3x() {
    Wire.beginTransmission(_addr);
    Wire.write(0x24); Wire.write(0x00);                     // single shot, high rep, no stretch
    if (Wire.endTransmission() != 0) return false;
    delay(16);
    uint8_t d[6];
    if (Wire.requestFrom(_addr, (uint8_t)6) != 6) return false;
    for (auto& b : d) b = Wire.read();
    if (_crc8(d, 2) != d[2] || _crc8(d + 3, 2) != d[5]) return false;
    uint16_t rt = (d[0] << 8) | d[1], rr = (d[3] << 8) | d[4];
    _t  = -45 + 175.0f * rt / 65535.0f;
    _rh = 100.0f * rr / 65535.0f;
    return true;
}

// ---------------- AHT20 / AHT21 ----------------
bool THSensor::_probeAht(uint8_t a) {
    _addr = a;
    delay(40);
    Wire.beginTransmission(a); Wire.write(0x71); Wire.endTransmission();
    if (Wire.requestFrom(a, (uint8_t)1) != 1) return false;
    uint8_t st = Wire.read();
    if (!(st & 0x08)) {                                     // not calibrated -> init
        Wire.beginTransmission(a); Wire.write(0xBE); Wire.write(0x08); Wire.write(0x00);
        Wire.endTransmission();
        delay(10);
    }
    return _readAht();
}
bool THSensor::_readAht() {
    Wire.beginTransmission(_addr);
    Wire.write(0xAC); Wire.write(0x33); Wire.write(0x00);
    if (Wire.endTransmission() != 0) return false;
    delay(80);
    uint8_t d[7];
    if (Wire.requestFrom(_addr, (uint8_t)7) != 7) return false;
    for (auto& b : d) b = Wire.read();
    if (d[0] & 0x80) return false;                          // still busy
    // AHT20 CRC (poly 0x31, init 0xFF) over the first 6 bytes
    if (_crc8(d, 6) != d[6]) return false;
    uint32_t rr = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t rt = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    _rh = rr * 100.0f / 1048576.0f;
    _t  = rt * 200.0f / 1048576.0f - 50;
    return true;
}

// ---------------- HTU21D / SHT21 / Si7021 ----------------
static uint8_t htuCrc(const uint8_t* d, uint8_t n) {        // poly 0x131, init 0
    uint8_t crc = 0;
    for (uint8_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
    return crc;
}
bool THSensor::_probeHtu(uint8_t a) {
    _addr = a;
    Wire.beginTransmission(a); Wire.write(0xFE); Wire.endTransmission();  // soft reset
    delay(15);
    return _readHtu();
}
bool THSensor::_readHtu() {
    auto measure = [this](uint8_t cmd, uint16_t& raw, uint16_t waitMs) -> bool {
        Wire.beginTransmission(_addr);
        Wire.write(cmd);                                    // no-hold master
        if (Wire.endTransmission() != 0) return false;
        delay(waitMs);
        uint8_t d[3];
        if (Wire.requestFrom(_addr, (uint8_t)3) != 3) return false;
        for (auto& b : d) b = Wire.read();
        if (htuCrc(d, 2) != d[2]) return false;
        raw = ((d[0] << 8) | d[1]) & 0xFFFC;
        return true;
    };
    uint16_t rt, rr;
    if (!measure(0xF3, rt, 55)) return false;
    if (!measure(0xF5, rr, 20)) return false;
    _t  = -46.85f + 175.72f * rt / 65536.0f;
    _rh = -6 + 125.0f * rr / 65536.0f;
    if (_rh < 0) _rh = 0;
    return true;
}
