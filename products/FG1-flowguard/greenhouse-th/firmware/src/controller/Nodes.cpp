#include "Nodes.h"

static uint32_t nowS() { return millis() / 1000; }
#ifdef FG1_REPEATER
static bool isZero(const uint8_t* m) { for (int i = 0; i < 6; i++) if (m[i]) return false; return true; }
// Sets NodeManager::_via for one scope (the path replies and commands take).
struct ViaScope {
    const uint8_t*& ref; const uint8_t* old;
    ViaScope(const uint8_t*& r, const uint8_t* v) : ref(r), old(r) { ref = (v && !isZero(v)) ? v : nullptr; }
    ~ViaScope() { ref = old; }
};
#endif

String NodeManager::_shortId(const uint8_t* mac) {
    char b[16];
    snprintf(b, sizeof(b), "%s_%02X%02X", NODE_PREFIX, mac[4], mac[5]);
    return String(b);
}

void NodeManager::begin(Preferences* prefs, const String& ctrlId) {
    _prefs = prefs;
    strlcpy(_ctrlId, ctrlId.c_str(), sizeof(_ctrlId));
    Persist p[MAX_NODES];
    size_t n = _prefs->getBytes("nodes", p, sizeof(p)) / sizeof(Persist);
    for (size_t i = 0; i < n && i < MAX_NODES; i++) {
        _nodes[i].used = true;
        memcpy(_nodes[i].mac, p[i].mac, 6);
        strlcpy(_nodes[i].name, p[i].name, sizeof(_nodes[i].name));
        _nodes[i].control = p[i].control;
    }
    _localControl = _prefs->getBool("localCtl", false);
    Serial.printf("[NODES] %u paired node(s)%s\n", count(), _localControl ? ", outside sensor in control" : "");
}

void NodeManager::_save() {
    Persist p[MAX_NODES];
    size_t n = 0;
    for (auto& nd : _nodes) {
        if (!nd.used) continue;
        memcpy(p[n].mac, nd.mac, 6);
        strlcpy(p[n].name, nd.name, sizeof(p[n].name));
        p[n].control = nd.control;
        n++;
    }
    if (n) _prefs->putBytes("nodes", p, n * sizeof(Persist));
    else   _prefs->remove("nodes");
    _prefs->putBool("localCtl", _localControl);
}

uint8_t NodeManager::count() const {
    uint8_t c = 0;
    for (auto& n : _nodes) if (n.used) c++;
    return c;
}

uint8_t NodeManager::onlineCount(uint32_t nowSec) const {
    uint8_t c = 0;
    for (auto& n : _nodes) if (n.used && n.everSeen && nowSec - n.lastSec <= sensorTimeoutSec) c++;
    return c;
}

NodeManager::Node* NodeManager::_find(const uint8_t* mac) {
    for (auto& n : _nodes) if (n.used && memcmp(n.mac, mac, 6) == 0) return &n;
    return nullptr;
}
NodeManager::Disc* NodeManager::_findDisc(const uint8_t* mac) {
    for (auto& d : _disc) if (d.used && memcmp(d.mac, mac, 6) == 0) return &d;
    return nullptr;
}
NodeManager::Disc* NodeManager::_discSlot(const uint8_t* mac) {
    if (Disc* d = _findDisc(mac)) return d;
    Disc* oldest = &_disc[0];
    for (auto& d : _disc) {
        if (!d.used) { oldest = &d; break; }
        if (d.lastMs < oldest->lastMs) oldest = &d;
    }
    *oldest = Disc();
    oldest->used = true;
    memcpy(oldest->mac, mac, 6);
    return oldest;
}

// ---------------- radio ----------------

void NodeManager::_sendTo(const uint8_t* mac, const void* data, size_t len) {
#ifdef FG1_REPEATER
    if (_via) {
        PktRelayDown d = {};
        protoFill(d.h, PKT_RELAY_DOWN);
        memcpy(d.origin, mac, 6);
        d.innerLen = len < sizeof(d.inner) ? len : sizeof(d.inner);
        memcpy(d.inner, data, d.innerLen);
        espnow.send(_via, &d, relayDownSize(d.innerLen));
        return;
    }
#endif
    espnow.send(mac, data, len);
}

void NodeManager::_sendAnnounce(const uint8_t* mac, bool knows) {
    PktAnnounce a = {};
    protoFill(a.h, PKT_ANNOUNCE);
    strlcpy(a.ctrlId, _ctrlId, sizeof(a.ctrlId));
    a.channel = EspNowLink::channel();
    a.knowsYou = knows;
    _sendTo(mac, &a, sizeof(a));
}

void NodeManager::_sendPair(const uint8_t* mac) {
    PktPair p = {};
    protoFill(p.h, PKT_PAIR);
    strlcpy(p.ctrlId, _ctrlId, sizeof(p.ctrlId));
    p.channel = EspNowLink::channel();
    _sendTo(mac, &p, sizeof(p));
}

void NodeManager::_sendSimple(const uint8_t* mac, PktType t) {
    PktHeader h;
    protoFill(h, t);
    _sendTo(mac, &h, sizeof(h));
}

void NodeManager::loop() {
    EspNowRx rx;
    while (espnow.poll(rx)) _handle(rx);
    // expire stale discoveries
    for (auto& d : _disc)
        if (d.used && !d.pending && millis() - d.lastMs > DISCOVERED_EXPIRE_MS) d.used = false;
}

void NodeManager::_handle(const EspNowRx& rx) {
    if (!protoValid(rx.data, rx.len)) return;
#ifdef FG1_REPEATER
    if (_noDirectId.length() && _shortId(rx.mac) == _noDirectId) return;   // bench: "out of range"
    switch (rx.data[3]) {
        case PKT_RELAY_UP:
            _onRelayUp(rx);
            return;
        case PKT_NODE_INFO:
            if (rx.len >= sizeof(PktNodeInfo)) _onNodeInfo(rx.mac, *(const PktNodeInfo*)rx.data);
            return;
    }
#endif
    switch (rx.data[3]) {
        case PKT_HELLO:
            if (rx.len >= sizeof(PktHello)) _onHello(rx, *(const PktHello*)rx.data);
            break;
        case PKT_PAIR_OK:
            _finishPair(rx.mac, "");
            break;
        case PKT_DATA:
            if (rx.len >= sizeof(PktData)) _onData(rx, *(const PktData*)rx.data);
            break;
    }
}

void NodeManager::_onHello(const EspNowRx& rx, const PktHello& p) {
    uint8_t myMac[6];
    esp_wifi_get_mac(WIFI_IF_STA, myMac);
    bool claimsMe = p.paired && memcmp(p.ctrlMac, myMac, 6) == 0;
    Node* n = _find(rx.mac);

    if (p.paired && !claimsMe) return;          // belongs to another controller

    if (claimsMe) {
        if (n) { _sendAnnounce(rx.mac, true); return; }
        // We were reset / it was removed while out of range.
        Serial.printf("[NODES] %s thinks it is mine but is not on my list -> UNPAIR\n", _shortId(rx.mac).c_str());
        _sendSimple(rx.mac, PKT_UNPAIR);
        return;
    }

    // Node is unpaired and looking for a controller.
    if (n && !n->resetByUser) {
        n->resetByUser = true;
        _event(String("Sensor ") + (n->name[0] ? n->name : _shortId(rx.mac).c_str()) + " was reset — add it again");
    }
    Disc* d = _discSlot(rx.mac);
    bool isNew = d->lastMs == 0;
#ifdef FG1_REPEATER
    // Heard directly in the last 10 s wins over a repeater's copy of the same HELLO.
    if (_via && d->directMs && millis() - d->directMs < 10000) return;
    if (_via) memcpy(d->via, _via, 6); else { memset(d->via, 0, 6); d->directMs = millis(); }
#endif
    d->lastMs = millis();
    d->rssi = rx.rssi;
    strlcpy(d->fw, p.fw, sizeof(d->fw));
    if (d->pending) {
        _sendPair(rx.mac);
    } else {
        _sendAnnounce(rx.mac, false);
        if (isNew) Serial.printf("[NODES] Discovered %s (RSSI %d)%s — waiting for Add in the app\n",
                                 _shortId(rx.mac).c_str(), rx.rssi, _via ? " via repeater" : "");
    }
}

void NodeManager::_finishPair(const uint8_t* mac, const char* fw) {
    Disc* d = _findDisc(mac);
    if (!d || !d->pending) return;
    Node* n = _find(mac);
    if (!n) {
        for (auto& s : _nodes) if (!s.used) { n = &s; break; }
        if (!n) { d->pending = false; return; }
        *n = Node();
        n->used = true;
        memcpy(n->mac, mac, 6);
        // First node chosen becomes the control input automatically.
        bool anyControl = _localControl;
        for (auto& o : _nodes) if (o.used && o.control) anyControl = true;
        n->control = !anyControl;
    }
    n->resetByUser = false;
#ifdef FG1_REPEATER
    if (_via) memcpy(n->via, _via, 6); else memset(n->via, 0, 6);
#endif
    strlcpy(n->fw, fw[0] ? fw : d->fw, sizeof(n->fw));
    d->used = false;
    _save();
    Serial.printf("[NODES] Paired %s\n", _shortId(mac).c_str());
    _event(String("Sensor ") + _shortId(mac) + " added" + (n->control ? " (control input)" : ""));
}

void NodeManager::_onData(const EspNowRx& rx, const PktData& p) {
    Node* n = _find(rx.mac);
    if (!n || n->resetByUser) {
        Disc* d = _findDisc(rx.mac);
        if (d && d->pending) { _finishPair(rx.mac, p.fw); n = _find(rx.mac); }
    }
    if (!n) {
        Serial.printf("[NODES] DATA from unknown %s -> UNPAIR\n", _shortId(rx.mac).c_str());
        _sendSimple(rx.mac, PKT_UNPAIR);
        return;
    }
    if (onData) onData();
    uint32_t now = nowS();
    bool wasOffline = !n->everSeen || now - n->lastSec > sensorTimeoutSec;
    if (n->rx && p.seq > n->lastSeq && p.seq - n->lastSeq < 1000) n->lost += p.seq - n->lastSeq - 1;
    n->lastSeq = p.seq;
    n->rx++;
    n->lastSec = now;
    n->everSeen = true;
    n->sensorOk = p.sensorOk;
    n->sensorKind = p.sensorKind;
    n->t = p.t_x100 / 100.0f;
    n->rh = p.rh_x100 / 100.0f;
    n->rssi = rx.rssi;
    n->rssiAtNode = p.rssiFromCtrl;
    n->uptime = p.uptimeSec;
    strlcpy(n->fw, p.fw, sizeof(n->fw));
#ifdef FG1_REPEATER
    if (_via) {
        if (memcmp(n->via, _via, 6) != 0) _event(String("Sensor ") + (n->name[0] ? n->name : _shortId(rx.mac).c_str()) + " now via " + _shortId(_via));
        memcpy(n->via, _via, 6);
    } else if (!isZero(n->via)) {
        _event(String("Sensor ") + (n->name[0] ? n->name : _shortId(rx.mac).c_str()) + " now direct");
        memset(n->via, 0, 6);
    }
    _sendDataAck(rx.mac, p.seq);
#endif
    if (p.sensorOk) { n->avgT.add(n->t, now); n->avgRh.add(n->rh, now); }
    if (wasOffline && n->rx > 1)
        _event(String("Sensor ") + (n->name[0] ? n->name : _shortId(rx.mac).c_str()) + " back online");
}

// ---------------- local sensor + control value ----------------

void NodeManager::updateLocal(bool ok, float t, float rh, uint32_t nowSec) {
    _localSensorOk = ok;
    if (ok) { _localT.add(t, nowSec); _localRh.add(rh, nowSec); }
}

bool NodeManager::localOk(uint32_t nowSec) const {
    return _localSensorOk && _localT.valid(nowSec, sensorTimeoutSec);
}

ControlReading NodeManager::control(uint32_t nowSec) const {
    ControlReading r;
    float st = 0, srh = 0;
    for (auto& n : _nodes) {
        if (!n.used || !n.control) continue;
        r.selected++;
        if (n.resetByUser || !n.sensorOk || !n.avgT.valid(nowSec, sensorTimeoutSec)) continue;
        st += n.avgT.value(nowSec);
        srh += n.avgRh.value(nowSec);
        r.used++;
    }
    bool useLocal = _localControl || r.selected == 0;
    if (_localControl) r.selected++;
    if (r.selected == 0) { r.fallback = true; }
    if (useLocal && localOk(nowSec)) {
        st += _localT.value(nowSec);
        srh += _localRh.value(nowSec);
        r.used++;
    }
    if (r.used) { r.ok = true; r.t = st / r.used; r.rh = srh / r.used; }
    return r;
}

// ---------------- app actions ----------------

bool NodeManager::add(const String& macS, String& err) {
    uint8_t mac[6];
    if (!EspNowLink::parseMac(macS, mac)) { err = "bad mac"; return false; }
    Node* n = _find(mac);
    if (n && !n->resetByUser) { err = "already added"; return false; }
    if (!n && count() >= MAX_NODES) { err = "max sensors reached"; return false; }
    Disc* d = _findDisc(mac);
    if (!d) { err = "sensor not seen recently — power it and wait 10 s"; return false; }
    d->pending = true;
    d->lastMs = millis();
    Serial.printf("[NODES] Add %s requested — pairing on its next hello\n", _shortId(mac).c_str());
    return true;
}

bool NodeManager::remove(const String& macS, String& err) {
    uint8_t mac[6];
    if (!EspNowLink::parseMac(macS, mac)) { err = "bad mac"; return false; }
    Disc* d = _findDisc(mac);
    if (d) d->used = false;                 // also cancels a pending add
    Node* n = _find(mac);
    if (!n) { if (d) return true; err = "not found"; return false; }
#ifdef FG1_REPEATER
    { ViaScope vs(_via, n->via); _sendSimple(mac, PKT_UNPAIR); }
#else
    _sendSimple(mac, PKT_UNPAIR);           // if it is in range it resets right away
#endif
    String label = n->name[0] ? String(n->name) : _shortId(mac);
    *n = Node();
    _save();
    _event("Sensor " + label + " removed");
    return true;
}

bool NodeManager::rename(const String& macS, const String& name, String& err) {
    uint8_t mac[6];
    if (!EspNowLink::parseMac(macS, mac)) { err = "bad mac"; return false; }
    Node* n = _find(mac);
    if (!n) { err = "not found"; return false; }
    strlcpy(n->name, name.c_str(), sizeof(n->name));
    _save();
    return true;
}

bool NodeManager::identify(const String& macS, String& err) {
    uint8_t mac[6];
    if (!EspNowLink::parseMac(macS, mac)) { err = "bad mac"; return false; }
    Node* n = _find(mac);
    if (!n) { err = "not found"; return false; }
#ifdef FG1_REPEATER
    ViaScope vs(_via, n->via);
#endif
    _sendSimple(mac, PKT_IDENTIFY);
    return true;
}

#ifdef FG1_REPEATER
bool NodeManager::setRelay(const String& macS, bool on, String& err) {
    uint8_t mac[6];
    if (!EspNowLink::parseMac(macS, mac)) { err = "bad mac"; return false; }
    Node* n = _find(mac);
    if (!n) { err = "not found"; return false; }
    if (!n->relayCap) { err = "this sensor has the standard (no repeater) firmware"; return false; }
    PktRelayCfg c = {};
    protoFill(c.h, PKT_RELAY_CFG);
    memcpy(c.origin, mac, 6);
    c.relayOn = on;
    ViaScope vs(_via, n->via);
    _sendTo(mac, &c, sizeof(c));
    n->relayOn = on;                         // NODE_INFO confirms it within a few seconds
    _event(String("Sensor ") + (n->name[0] ? n->name : _shortId(mac).c_str()) + (on ? " may relay" : " will not relay"));
    return true;
}

void NodeManager::_sendDataAck(const uint8_t* mac, uint16_t seq) {
    PktDataAck a = {};
    protoFill(a.h, PKT_DATA_ACK);
    memcpy(a.origin, mac, 6);
    a.seq = seq;
    _sendTo(mac, &a, sizeof(a));
}

void NodeManager::_onNodeInfo(const uint8_t* mac, const PktNodeInfo& p) {
    Node* n = _find(mac);
    if (!n) return;
    n->relayCap = true;
    n->relayOn = p.relayOn;
    n->carrying = p.children;
}

// A repeater forwards a far node's packet: handle it as if it came from the
// far node, with replies going back through the same repeater.
void NodeManager::_onRelayUp(const EspNowRx& rx) {
    if (rx.len < relayUpSize(0)) return;
    const PktRelayUp* u = (const PktRelayUp*)rx.data;
    if (u->innerLen > sizeof(u->inner) || rx.len < relayUpSize(u->innerLen)) return;
    if (!protoValid(u->inner, u->innerLen)) return;
    Node* rep = _find(rx.mac);
    if (!rep) return;                        // only our own paired nodes may relay
    EspNowRx in = {};
    memcpy(in.mac, u->origin, 6);
    in.rssi = u->rssi;
    in.len = u->innerLen;
    memcpy(in.data, u->inner, u->innerLen);
    ViaScope vs(_via, rx.mac);
    switch (in.data[3]) {
        case PKT_HELLO:
            if (in.len >= sizeof(PktHello)) _onHello(in, *(const PktHello*)in.data);
            break;
        case PKT_PAIR_OK:
            _finishPair(in.mac, "");
            break;
        case PKT_DATA:
            if (in.len >= sizeof(PktData)) _onData(in, *(const PktData*)in.data);
            break;
        case PKT_NODE_INFO:
            if (in.len >= sizeof(PktNodeInfo)) _onNodeInfo(in.mac, *(const PktNodeInfo*)in.data);
            break;
    }
}
#endif

void NodeManager::setControlSources(JsonArrayConst sources) {
    _localControl = false;
    for (auto& n : _nodes) n.control = false;
    for (JsonVariantConst v : sources) {
        String s = v.as<String>();
        if (s == "local") { _localControl = true; continue; }
        uint8_t mac[6];
        if (EspNowLink::parseMac(s, mac)) if (Node* n = _find(mac)) n->control = true;
    }
    _save();
    String txt = "Control input: ";
    uint8_t k = 0;
    for (auto& n : _nodes) if (n.used && n.control) { txt += (k++ ? ", " : "") + String(n.name[0] ? n.name : _shortId(n.mac).c_str()); }
    if (_localControl) { txt += (k++ ? ", " : ""); txt += "Outside"; }
    if (!k) txt += "Outside (no sensor chosen)";
    _event(txt);
}

void NodeManager::toJson(JsonObject root, uint32_t now) const {
    JsonArray arr = root["nodes"].to<JsonArray>();
    for (auto& n : _nodes) {
        if (!n.used) continue;
        JsonObject o = arr.add<JsonObject>();
        o["mac"] = EspNowLink::macStr(n.mac);
        o["id"] = _shortId(n.mac);
        o["name"] = n.name;
        o["control"] = n.control;
        bool online = n.everSeen && now - n.lastSec <= sensorTimeoutSec && !n.resetByUser;
        o["online"] = online;
        o["reset"] = n.resetByUser;
        if (n.everSeen) {
            o["age"] = now - n.lastSec;
            o["ok"] = n.sensorOk;
            o["t"] = serialized(String(n.t, 1));
            o["rh"] = serialized(String(n.rh, 1));
            o["rssi"] = n.rssi;
            o["rssi_node"] = n.rssiAtNode;
            o["rx"] = n.rx;
            o["lost"] = n.lost;
            o["fw"] = n.fw;
            o["up"] = n.uptime;
            o["sensor"] = n.sensorKind;
        }
#ifdef FG1_REPEATER
        if (!isZero(n.via)) o["via"] = _shortId(n.via);       // rssi above is then the far hop
        if (n.relayCap) { o["relay"] = n.relayOn; o["carrying"] = n.carrying; }
#endif
    }
    JsonArray disc = root["discovered"].to<JsonArray>();
    for (auto& d : _disc) {
        if (!d.used) continue;
        JsonObject o = disc.add<JsonObject>();
        o["mac"] = EspNowLink::macStr(d.mac);
        o["id"] = _shortId(d.mac);
        o["rssi"] = d.rssi;
        o["fw"] = d.fw;
        o["pending"] = d.pending;
#ifdef FG1_REPEATER
        if (!isZero(d.via)) o["via"] = _shortId(d.via);
#endif
        o["age"] = (millis() - d.lastMs) / 1000;
    }
    root["local_control"] = _localControl;
#ifdef FG1_REPEATER
    root["repeater"] = true;                 // repeater-version controller
#endif
}
