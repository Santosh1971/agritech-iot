#pragma once
// Credentials live in Secrets.h (git-ignored; template Secrets.example.h).
#if __has_include("Secrets.h")
#include "Secrets.h"
#else
#error "Copy include/Secrets.example.h to include/Secrets.h and fill in the values"
#endif
// FG1 Polyhouse — shared configuration for the controller and node builds.

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION    "0.1.0"
#endif

// ---------- Identity ----------
// Controller ID = "FG1P_" + last 4 hex of the eFuse MAC, e.g. FG1P_9E74.
// Same value is the SoftAP name and the MQTT topic key.
#define DEVICE_PREFIX       "FG1P"
#define NODE_PREFIX         "FG1N"

// ---------- MQTT (same broker + shared credential as FG1/WPC) ----------
#define MQTT_BROKER         "mqtt.agrisenseandcontrol.in"
#define MQTT_PORT           1883
#define MQTT_TOPIC_ROOT     "agrisense/FG1/"   // broker ACL only passes agrisense/FG1|WPC/...; FG1P_ ids keep us apart

// ---------- SoftAP fallback ----------
#define SOFTAP_CHANNEL          1
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RETRY_INTERVAL_MS  60000

// ---------- OTA (/update on the controller's local server) ----------

// ---------- GPIO (FG1 board, ESP32 30-pin devkit) ----------
#define RELAY_PIN           19    // RELAY1_MCU -> ULN2003 -> relay (controller only)
#define WIFI_LED_PIN        2     // on-board LED: link status
#define FLOW_SENSOR_LED_PIN 32    // green LED: blinks on each ESP-NOW packet
#define BOOT_BUTTON_PIN     0     // hold 5 s: node forgets its controller / controller forgets WiFi
#define I2C_SDA             21
#define I2C_SCL             22

// ---------- Timing ----------
#define SENSOR_READ_MS          5000    // controller's own sensor
#define NODE_SEND_MS            10000   // node -> controller reading
#define STATUS_PUBLISH_MS       5000
#define TREND_SLOT_SEC          300     // 5-min trend points, 24 h kept in RAM
#define TREND_POINTS            288
#define EVENT_LOG_LEN           60

// ---------- Nodes ----------
#define MAX_NODES               8
#define MAX_DISCOVERED          6
#define DISCOVERED_EXPIRE_MS    60000
