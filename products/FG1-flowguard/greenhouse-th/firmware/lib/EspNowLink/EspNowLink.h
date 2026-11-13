#pragma once
// Thin ESP-NOW wrapper shared by the controller and node builds.
//  - Received packets are copied into a FreeRTOS queue from the WiFi task
//    callback and handled from loop() via poll(), never inside the callback.
//  - RSSI: Arduino-ESP32 2.x's recv callback carries no RSSI, so a
//    promiscuous-mode callback (management frames only) records the RSSI
//    of the last vendor action frame per sender — that is the ESP-NOW frame
//    the recv callback sees right after. Needed for the 45 m range test.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

struct EspNowRx {
    uint8_t mac[6];
    int8_t  rssi;          // 0 = unknown
    uint8_t len;
    uint8_t data[96];
};

class EspNowLink {
public:
    bool begin();                                  // WiFi.mode() must be set first
    bool poll(EspNowRx& out);                      // true if a packet was waiting
    bool send(const uint8_t* mac, const void* data, size_t len);
    bool broadcast(const void* data, size_t len);
    void removePeer(const uint8_t* mac);

    // Node only: move the radio to a channel (not connected to any AP).
    static void setChannel(uint8_t ch);
    static uint8_t channel();

    // Result of the last unicast send, from the send callback.
    // sendDone() goes true once the MAC-layer ACK (or its failure) is known.
    bool sendDone() const { return _sendDone; }
    bool lastSendOk() const { return _lastOk; }
    uint32_t sentOk() const { return _sentOk; }
    uint32_t sentFail() const { return _sentFail; }

    static String macStr(const uint8_t* mac);      // "A1B2C3D4E5F6"
    static bool   parseMac(const String& s, uint8_t* mac);

    // internal (called from C callbacks)
    void _onRecv(const uint8_t* mac, const uint8_t* data, int len);
    void _onSent(const uint8_t* mac, esp_now_send_status_t st);
    void _onPromisc(const uint8_t* src, int8_t rssi);

private:
    bool _ensurePeer(const uint8_t* mac);
    int8_t _rssiFor(const uint8_t* mac);

    QueueHandle_t _q = nullptr;
    volatile bool _sendDone = true;
    volatile bool _lastOk = false;
    volatile uint32_t _sentOk = 0, _sentFail = 0;

    static const uint8_t RSSI_SLOTS = 12;
    struct RssiSlot { uint8_t mac[6]; int8_t rssi; };
    RssiSlot _rssi[RSSI_SLOTS] = {};
    uint8_t  _rssiNext = 0;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};

extern EspNowLink espnow;
