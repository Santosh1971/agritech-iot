#include "EspNowLink.h"

EspNowLink espnow;

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void recvCb(const uint8_t* mac, const uint8_t* data, int len) { espnow._onRecv(mac, data, len); }
static void sentCb(const uint8_t* mac, esp_now_send_status_t st)     { espnow._onSent(mac, st); }

typedef struct {
    uint16_t frame_ctrl;
    uint16_t duration;
    uint8_t  addr1[6];
    uint8_t  addr2[6];   // transmitter
    uint8_t  addr3[6];
    uint16_t seq;
} __attribute__((packed)) MacHdr;

static void IRAM_ATTR promiscCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
    const MacHdr* h = (const MacHdr*)p->payload;
    if ((h->frame_ctrl & 0x00FC) != 0x00D0) return;          // action frames only
    espnow._onPromisc(h->addr2, (int8_t)p->rx_ctrl.rssi);
}

bool EspNowLink::begin() {
    if (!_q) _q = xQueueCreate(12, sizeof(EspNowRx));
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] init failed");
        return false;
    }
    esp_now_register_recv_cb(recvCb);
    esp_now_register_send_cb(sentCb);
    _ensurePeer(BCAST);

    wifi_promiscuous_filter_t f = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous_rx_cb(promiscCb);
    esp_wifi_set_promiscuous(true);
    Serial.printf("[ESPNOW] ready on channel %u, MAC %s\n", channel(), WiFi.macAddress().c_str());
    return true;
}

void EspNowLink::_onPromisc(const uint8_t* src, int8_t rssi) {
    portENTER_CRITICAL_ISR(&_mux);
    uint8_t slot = RSSI_SLOTS;
    for (uint8_t i = 0; i < RSSI_SLOTS; i++)
        if (memcmp(_rssi[i].mac, src, 6) == 0) { slot = i; break; }
    if (slot == RSSI_SLOTS) { slot = _rssiNext; _rssiNext = (_rssiNext + 1) % RSSI_SLOTS; memcpy(_rssi[slot].mac, src, 6); }
    _rssi[slot].rssi = rssi;
    portEXIT_CRITICAL_ISR(&_mux);
}

int8_t EspNowLink::_rssiFor(const uint8_t* mac) {
    int8_t r = 0;
    portENTER_CRITICAL(&_mux);
    for (uint8_t i = 0; i < RSSI_SLOTS; i++)
        if (memcmp(_rssi[i].mac, mac, 6) == 0) { r = _rssi[i].rssi; break; }
    portEXIT_CRITICAL(&_mux);
    return r;
}

void EspNowLink::_onRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (!_q || len <= 0) return;
    EspNowRx rx;
    memcpy(rx.mac, mac, 6);
    rx.len = (uint8_t)min(len, (int)sizeof(rx.data));
    memcpy(rx.data, data, rx.len);
    rx.rssi = _rssiFor(mac);
    xQueueSend(_q, &rx, 0);
}

void EspNowLink::_onSent(const uint8_t* mac, esp_now_send_status_t st) {
    if (memcmp(mac, BCAST, 6) == 0) return;
    _lastOk = (st == ESP_NOW_SEND_SUCCESS);
    if (_lastOk) _sentOk++; else _sentFail++;
    _sendDone = true;
}

bool EspNowLink::poll(EspNowRx& out) {
    return _q && xQueueReceive(_q, &out, 0) == pdTRUE;
}

bool EspNowLink::_ensurePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;                       // = current channel
    p.ifidx   = WIFI_IF_STA;
    p.encrypt = false;
    esp_err_t e = esp_now_add_peer(&p);
    if (e == ESP_ERR_ESPNOW_FULL) {
        // Drop every unicast peer and retry; peers are cheap to re-add.
        esp_now_peer_info_t it;
        bool from_head = true;
        uint8_t victims[20][6]; int n = 0;
        while (n < 20 && esp_now_fetch_peer(from_head, &it) == ESP_OK) {
            from_head = false;
            if (memcmp(it.peer_addr, BCAST, 6) != 0) memcpy(victims[n++], it.peer_addr, 6);
        }
        for (int i = 0; i < n; i++) esp_now_del_peer(victims[i]);
        e = esp_now_add_peer(&p);
    }
    return e == ESP_OK;
}

void EspNowLink::removePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
}

bool EspNowLink::send(const uint8_t* mac, const void* data, size_t len) {
    if (!_ensurePeer(mac)) return false;
    _sendDone = false;
    esp_err_t e = esp_now_send(mac, (const uint8_t*)data, len);
    if (e != ESP_OK) { _sendDone = true; _lastOk = false; _sentFail++; }
    return e == ESP_OK;
}

bool EspNowLink::broadcast(const void* data, size_t len) {
    return esp_now_send(BCAST, (const uint8_t*)data, len) == ESP_OK;
}

void EspNowLink::setChannel(uint8_t ch) {
    // Promiscuous is already on (begin()), which lets set_channel work
    // without an AP association.
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

uint8_t EspNowLink::channel() {
    uint8_t ch = 0; wifi_second_chan_t s;
    esp_wifi_get_channel(&ch, &s);
    return ch;
}

String EspNowLink::macStr(const uint8_t* m) {
    char b[13];
    snprintf(b, sizeof(b), "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(b);
}

bool EspNowLink::parseMac(const String& s, uint8_t* mac) {
    if (s.length() != 12) return false;
    for (int i = 0; i < 6; i++) {
        char hex[3] = {s[i * 2], s[i * 2 + 1], 0};
        char* end;
        long v = strtol(hex, &end, 16);
        if (*end) return false;
        mac[i] = (uint8_t)v;
    }
    return true;
}
