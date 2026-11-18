#pragma once
// Board pin maps. One firmware, two boards:
//
//   (default)          WM1 Water Manager-Mini: ESP32 devkit, relays and
//                      status LEDs on two 74HC595s, flow/pressure/battery
//                      on ADC1 pins.
//   BOARD_WM_MINI_C3   WM-Mini C3: ESP32-C3 Super Mini, the six relays
//                      driven straight from GPIOs through a ULN2003, flow
//                      input, DS1307, WiFi LED (IO8) and flow LED (IO21).
//                      No pressure, battery or water-level inputs.
//                      Hardware: products/WM1-watermaster/hardware/WM-MiniC3
//
// The C3 build also runs unchanged on the FG1 / ASC Mini-C3 carrier (same
// Super Mini, same pins for flow IO5, DS1307 IO6/IO7 and relay IO20), which
// is what we test on before WM-Mini C3 boards exist: pump = its relay,
// doser = its STATUS LED (IO10), V3 = its 5 V pump output (IO3).

#include <Arduino.h>

static constexpr uint8_t NO_PIN = 0xFF;

#if defined(BOARD_WM_MINI_C3)

#define BOARD_NAME "WM-Mini-C3"

// Relay order is the WM1 order: RL1 pump, RL2 doser, RL3-RL6 valves V1-V4.
// ULN2003 inputs, active HIGH. IO20 is UART0 RX (an input during boot), so
// the pump relay stays off while the board boots.
static constexpr uint8_t RELAY_PINS[6] = {20, 10, 1, 0, 3, 4};

static constexpr uint8_t PIN_I2C_SDA = 6;
static constexpr uint8_t PIN_I2C_SCL = 7;

static constexpr uint8_t PIN_FLOW_IN   = 5;    // Schottky + 10k level shift, 5 V TVS
static constexpr uint8_t PIN_FLOW_LED  = 21;   // flow LED, active HIGH (UART0 TX: flickers during the ROM boot log)
static constexpr uint8_t PIN_WIFI_LED  = 8;    // Super Mini blue LED, active LOW
static constexpr bool    WIFI_LED_ACTIVE_LOW = true;

static constexpr uint8_t PIN_PRESS1    = NO_PIN;
static constexpr uint8_t PIN_PRESS2    = NO_PIN;
static constexpr uint8_t PIN_BATT      = NO_PIN;
static constexpr uint8_t PIN_WATER_L1  = NO_PIN;
static constexpr uint8_t PIN_WATER_L2  = NO_PIN;

#else

#define BOARD_NAME "WM1-Mini"

#define RLY_DATA  17
#define RLY_CLK   16
#define RLY_LATCH 13

static constexpr uint8_t PIN_I2C_SDA = 21;
static constexpr uint8_t PIN_I2C_SCL = 22;

static constexpr uint8_t PIN_FLOW_IN   = 36;
static constexpr uint8_t PIN_FLOW_LED  = NO_PIN;  // on the 74HC595 (LED_FLOW bit)
static constexpr uint8_t PIN_WIFI_LED  = 2;
static constexpr bool    WIFI_LED_ACTIVE_LOW = false;

static constexpr uint8_t PIN_PRESS1    = 39;
static constexpr uint8_t PIN_PRESS2    = 34;
static constexpr uint8_t PIN_BATT      = 35;
static constexpr uint8_t PIN_WATER_L1  = 32;  // IN2, repurposed
static constexpr uint8_t PIN_WATER_L2  = 33;  // IN3, repurposed

#endif
