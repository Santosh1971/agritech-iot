#pragma once
// Repeater version (FG1_REPEATER): which way a node sends, and the list of far
// nodes a repeater carries. Plain C++, unit-tested on the host (test_relay).
#include <stdint.h>
#include <string.h>

namespace relay {

// Below this signal a direct link counts as weak and a repeater may be used.
static const int8_t WEAK_RSSI = -85;
// A relayed node goes back to direct only when direct is this much better.
static const int8_t BACK_MARGIN = 5;

// 0 means "not measured": treat as a fair signal rather than as a dead link.
inline int8_t eff(int8_t rssi) { return rssi == 0 ? -60 : rssi; }

// A path through a repeater is as good as its weaker hop.
inline int8_t offerScore(int8_t heardRepeater, int8_t repeaterToCtrl) {
    int8_t a = eff(heardRepeater), b = eff(repeaterToCtrl);
    return a < b ? a : b;
}

enum class Path : uint8_t { None, Direct, Relay };

// Direct wins when it is good enough, when there is no offer, or when it is
// at least as good as the best offer. Otherwise the repeater.
inline Path choose(bool haveDirect, int8_t directRssi, bool haveOffer, int8_t bestOfferScore,
                   int8_t weak = WEAK_RSSI) {
    if (haveDirect) {
        int8_t d = eff(directRssi);
        if (d >= weak || !haveOffer || d >= bestOfferScore) return Path::Direct;
    }
    return haveOffer ? Path::Relay : Path::None;
}

inline bool directGoodEnough(int8_t rssi, int8_t weak = WEAK_RSSI) { return eff(rssi) >= weak; }
inline bool switchBackToDirect(int8_t rssi, int8_t weak = WEAK_RSSI) { return eff(rssi) >= weak + BACK_MARGIN; }

// Far nodes a repeater carries (RAM only; a node that goes silent drops out).
struct ChildTable {
    static const uint8_t  MAX = 4;
    static const uint32_t EXPIRE_MS = 5UL * 60 * 1000;   // 5 min without a packet

    bool has(const uint8_t* mac) const { return _find(mac) >= 0; }
    bool canAccept(const uint8_t* mac) const { return has(mac) || count() < MAX; }

    // Add or refresh. False if the table is full and mac is new.
    bool touch(const uint8_t* mac, uint32_t nowMs) {
        int i = _find(mac);
        if (i < 0) {
            for (uint8_t k = 0; k < MAX; k++) if (!_used[k]) { i = k; break; }
            if (i < 0) return false;
            _used[i] = true;
            memcpy(_mac[i], mac, 6);
        }
        _last[i] = nowMs;
        return true;
    }
    void remove(const uint8_t* mac) { int i = _find(mac); if (i >= 0) _used[i] = false; }
    void expire(uint32_t nowMs) {
        for (uint8_t k = 0; k < MAX; k++)
            if (_used[k] && (uint32_t)(nowMs - _last[k]) > EXPIRE_MS) _used[k] = false;
    }
    uint8_t count() const { uint8_t c = 0; for (uint8_t k = 0; k < MAX; k++) c += _used[k]; return c; }

private:
    int _find(const uint8_t* mac) const {
        for (uint8_t k = 0; k < MAX; k++) if (_used[k] && memcmp(_mac[k], mac, 6) == 0) return k;
        return -1;
    }
    bool     _used[MAX] = {};
    uint8_t  _mac[MAX][6] = {};
    uint32_t _last[MAX] = {};
};

} // namespace relay
