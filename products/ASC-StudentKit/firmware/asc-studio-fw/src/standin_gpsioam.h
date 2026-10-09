// Stand-in board: the GPSIOAM 2026 workshop kit (ESP32 DevKit V1, classic
// ESP32-WROOM, CP2102 USB-UART), so the studio can be tried on hardware
// colleges already have. Its ports keep the Mini's names, so a Mini design
// runs on it if it only uses the ports the kit has:
//   S1 = GPIO23  DHT22 data
//   S2 = GPIO35  YF-S201 flow sensor pulses (input only, about 450 pulses per litre)
//   OUT1 = GPIO19  relay for the pump (HIGH = ON)
//   OUT2 = GPIO2   on-board blue LED
//   RTC  = DS1307 or DS3231 on SDA 21 / SCL 22
//   PAIR = the BOOT button (GPIO0)
// There is no S3, S4 or student I2C port: the board refuses a design that uses them.
// Built with `pio run -e asc_gpsioam`; never used on a real ASC kit.
#pragma once
#include "board_map.h"

static const BoardMap GPSIOAM_BOARD = {
  .name = "mini",
  .sensor = {23, 35, -1, -1, -1, -1, -1, -1},
  .out = {19, 2, -1, -1},
  .sensorPorts = 2, .i2cPorts = 0, .relays = 2,
  .i2cExtSda = -1, .i2cExtScl = -1, .i2cIntSda = 21, .i2cIntScl = 22,
  .led = -1, .buzzer = -1, .pairBtn = 0, .rtcInt = -1, .vinSense = -1, .vbatAdc = -1, .vbatEn = -1, .valve = -1,
};
