// Stand-in board: the GPSIOAM 2026 workshop kit (ESP32 DevKit V1, classic
// ESP32, CP2102 USB-UART), so the studio can be tried on hardware colleges
// already have. Its ports keep the Mini's names, so a Mini design runs on it
// unchanged; only the pins differ. Wiring is the workshop labs' own
// (docs/workshops/gpsioam-2026/labs):
//   S1 = GPIO34  soil sensor / potentiometer (ADC1, input only)  - lab 2B
//   S2 = GPIO14  float switch to GND                              - lab 2B
//   S3 = GPIO4   DHT22 data                                       - lab 1
//   S4 = GPIO27  flow sensor pulses                               - lab 2A
//   OUT1 = GPIO26  pump LED / relay                               - lab 2B
//   OUT2 = GPIO2   on-board blue LED
//   I2C-1 = SDA 21 / SCL 22 (the DevKit's usual I2C pins)
//   PAIR = the BOOT button (GPIO0)
// Built with `pio run -e asc_gpsioam`; never used on a real ASC kit.
#pragma once
#include "board_map.h"

static const BoardMap GPSIOAM_BOARD = {
  .name = "mini",
  .sensor = {34, 14, 4, 27, -1, -1, -1, -1},
  .out = {26, 2, -1, -1},
  .sensorPorts = 4, .i2cPorts = 1, .relays = 2,
  .i2cExtSda = 21, .i2cExtScl = 22, .i2cIntSda = -1, .i2cIntScl = -1,
  .led = -1, .buzzer = -1, .pairBtn = 0, .rtcInt = -1, .vinSense = -1, .vbatAdc = -1, .vbatEn = -1, .valve = -1,
};
