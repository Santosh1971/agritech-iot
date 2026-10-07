// Bluetooth LE link for the ASC Studio app. It carries exactly the same
// JSON-lines protocol as USB serial, over a Nordic-UART-style service:
// the phone writes command lines to RX and gets reply lines as TX notifications.
#pragma once
#include <Arduino.h>

namespace ble {

// Service and characteristic UUIDs (the widely used "Nordic UART" set, so
// generic BLE terminal apps can also talk to a kit for debugging).
static const char* SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

void begin(const String& deviceName);
bool connected();
// A complete command line received from the phone, if any (handled in loop(), never in the BLE callback).
bool takeLine(String& out);
// Send one reply line (a '\n' is added), split to fit the link's MTU.
void sendLine(const String& line);

}  // namespace ble
