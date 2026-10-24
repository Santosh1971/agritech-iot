#pragma once
// WM2-WaterMaster pin map (ESP32-WROOM-32D, U4).
// Source of truth: hardware/WaterMaster/WM2-WaterMaster.kicad_sch
// (extracted from the KiCad netlist, 2026-10-24). Shared by selftest and
// the main firmware -- change it here only if the schematic changes.

#include <stdint.h>

namespace wm2 {

// --- Sensor inputs (input-only pins, 10K pull-up to 3.3V on board,
//     470R series + 100pF to GND). Idle with nothing plugged in = HIGH.
constexpr uint8_t PIN_FLOW[4] = {34, 35, 36, 39};  // J2, J5, J3, J4 -> Flow_MCU1..4
constexpr uint8_t PIN_PS      = 32;                // J1 pressure switch, closes to GND (active LOW)

// --- Indicator LEDs (active HIGH, 470R). LD1 power LED is hardwired to +12V.
constexpr uint8_t PIN_LED_FLOW[4] = {25, 26, 19, 18};  // LD3..LD6
constexpr uint8_t PIN_LED_PS      = 27;                // LD7
constexpr uint8_t PIN_LED_WIFI    = 14;                // LD2 (blue)

// --- Latching-valve drivers, DRV8871 (U1 = V1/J7, U2 = V2/J8).
//     IN1=H IN2=L -> OUT1 high (forward) | IN1=L IN2=H -> reverse
//     both LOW -> coast, chip sleeps after ~1ms. Never leave both HIGH (brake).
//     ILIM 22K -> ~2.9A. VM comes from +12V_LV (solar/battery), not from USB 5V.
//     Direction LEDs across each output pair: Green / Yellow (LD8-LD11).
constexpr uint8_t PIN_V1_IN1 = 13;
constexpr uint8_t PIN_V1_IN2 = 4;
constexpr uint8_t PIN_V2_IN1 = 16;
constexpr uint8_t PIN_V2_IN2 = 17;

// --- Battery / solar 12V monitor: +12V -> 56.2K -> node -> 10K -> GND,
//     node -> 470R -> IO33 (100nF filter).
constexpr uint8_t PIN_BATT_ADC = 33;
constexpr float   BATT_DIVIDER = (56.2f + 10.0f) / 10.0f;  // 6.62

// --- DS1307Z+ RTC (U5, VCC 5V, CR2032 backup), 4K7 pull-ups to 3.3V.
constexpr uint8_t PIN_SDA = 21;
constexpr uint8_t PIN_SCL = 22;
constexpr uint8_t RTC_I2C_ADDR = 0x68;

// Unconnected / free: IO2, IO5, IO12, IO15, IO23 (IO2/5/12/15 are strapping pins).

}  // namespace wm2
