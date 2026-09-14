#pragma once

// ---------- Device ----------
#define DEVICE_ID           "SWC_001"
// CI overrides this via scripts/inject_firmware_version.py (FIRMWARE_VERSION_OVERRIDE
// env var, set to dev-<shortsha> for auto-builds) — bump this by hand to match
// whenever cutting an actual dashboard release, e.g. "1.1.0".
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION    "1.2.0"
#endif

// ---------- BLE ----------
#define BLE_DEVICE_NAME     "SmartWaterCtrl"
#define BLE_SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define BLE_CHAR_RX_UUID        "12345678-1234-1234-1234-123456789abd"
#define BLE_CHAR_TX_UUID        "12345678-1234-1234-1234-123456789abe"

// ---------- MQTT ----------
#define MQTT_BROKER         "mqtt.agrisenseandcontrol.in"
#define MQTT_PORT           1883
#define MQTT_USER           "fg1-device"
#define MQTT_PASS           "asacfg1"
// Topics are now built at runtime in MQTTClient.cpp from the actual
// per-device ID (see computeDeviceId() in main.cpp) — these used to be
// fixed here at compile time as "swc/SWC_001/...", which every physical
// device shared identically, a real collision risk with more than one
// unit in Cloud mode at once.
// ---------- WiFi fallback (SoftAP, no field WiFi available) ----------
#define SOFTAP_SSID             DEVICE_ID
#define SOFTAP_PASSWORD         "water1234"  // TODO: derive per-device password for production
#define WIFI_CONNECT_TIMEOUT_MS 15000        // boot-time connect attempt
#define WIFI_RETRY_INTERVAL_MS  60000        // background retry while in fallback

// ---------- OTA (bench SoftAP flash + WiFi flash from the app) ----------
// Gates /update (ElegantOTA's own portal + upload endpoint) so that once a
// device is reachable over a customer's home WiFi (Cloud mode), anyone else
// on that same network can't reflash it — reachable-but-unauthenticated was
// an acceptable bench tradeoff on an isolated SoftAP with a developer
// physically present; it stopped being one the moment WiFi OTA started
// working over STA. Must match WifiOtaFlasher.kt's OTA_USERNAME/PASSWORD on
// the app side — same TODO as SOFTAP_PASSWORD above about deriving this
// per-device before this ships to a dealer network.
#define OTA_USERNAME            "nbagri"
#define OTA_PASSWORD            "flash-nb-2026"

// ---------- GPIO ----------
// Pin assignments per current FG1 schematic (ESP32-30pin dev kit sheet).
#define RELAY_PIN           19    // D19 — RELAY1_MCU (relay driver input via ULN2003)
// No separate relay/pump LED GPIO exists on this board — LED_YELLOW_PUMP is
// wired on the ULN2003 driver's output side (next to the relay coil), so it
// lights automatically whenever the relay fires. RelayControl.begin() is
// called with no LED pin (defaults to 255 = "no LED wired").
#define FLOW_SENSOR_PIN     35    // D35 — FLOW1_MCU (input-only pin; flow pulse input)
#define FLOW_SENSOR_LED_PIN 32    // D32 — FLOW1_LED_MCU (flow sensor status LED)
#define WIFI_LED_PIN        2     // D2/OnBoardLED — doubles as WiFi + power status LED
#define I2C_SDA             21    // D21/I2C_SDA
#define I2C_SCL             22    // D22/I2C_SCL

// ---------- Flow Sensor ----------
#define DEFAULT_PULSES_PER_LITER  450

// ---------- Scheduler ----------
#define MAX_CYCLES          4
#define NVS_NAMESPACE       "swc"
// Packed into a single NVS blob (~sizeof(HistoryEntry) * this many bytes,
// roughly 68 bytes/entry). NVS partition here is only ~20KB total shared
// with cycles/WiFi/MQTT/running-state, and NVS itself needs headroom
// beyond the partition's nominal size for its own page/wear-leveling
// bookkeeping — 200 entries (~13KB) left too little margin and caused
// real "NOT_ENOUGH_SPACE" failures in testing. 60 entries (~4KB) leaves
// comfortable room; still ~12 days of history at 5 events/day.
#define HISTORY_MAX_ENTRIES 60   // ~12 days at moderate use (5 events/day)

// ---------- Timing ----------
#define STATUS_PUBLISH_INTERVAL_MS   5000
#define SCHEDULE_CHECK_INTERVAL_MS   1000
#define STATE_SAVE_INTERVAL_MS       10000

// ---------- LED Blink Patterns ----------
// WiFi LED
#define WIFI_LED_CONNECTING_ON_MS    200   // fast blink = connecting
#define WIFI_LED_CONNECTING_OFF_MS   200
#define WIFI_LED_CONNECTED_ON_MS     1000  // slow blink = connected, no MQTT
#define WIFI_LED_CONNECTED_OFF_MS    1000
#define WIFI_LED_MQTT_ON_MS          50    // double blink = WiFi + MQTT ok
#define WIFI_LED_MQTT_OFF_MS         50

// Flow LED
#define FLOW_LED_PULSE_MS            50    // brief flash per pulse burst
