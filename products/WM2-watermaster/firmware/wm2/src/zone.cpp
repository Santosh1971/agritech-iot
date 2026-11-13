#include "zone.h"

#include <WiFi.h>
#include <WiFiUdp.h>

#include <vector>

#include "app.h"
#include "hw.h"
#include "net.h"
#include "settings.h"
#include "zone_seq.h"

using namespace wm2core;

namespace zone {

static const uint16_t PORT = 47811;
static const uint32_t LEADER_LOST_MS = 60000;
static const uint32_t MEMBER_KEEP_MS = 10UL * 60 * 1000;  // forget silent members after 10 min

struct Entry {
  String dev;
  uint8_t v;  // 0 = V1, 1 = V2
};
struct Member {
  String dev;
  uint32_t heardMs = 0;
  IPAddress ip;  // learnt from its reports: the leader also sends to it directly
  bool due[2] = {false, false}, done[2] = {false, false}, open[2] = {false, false};
};

static WiFiUDP udp;
static bool udpOn = false;
static uint32_t txOk = 0, txErr = 0, rxN = 0;

// leader
static std::vector<std::vector<Entry>> turns;
static String parsedFrom = "\x01";  // cfg.seqTurns the turns were parsed from
static std::vector<Member> members;
static uint8_t st[SEQ_MAX_TURNS][SEQ_MAX_PER_TURN];
static int curTurn = -1;
static bool water = false;
static uint32_t lastLeaderTx = 0;

// member
static uint32_t leaderHeardMs = 0;
static IPAddress leaderIp;  // learnt from its packets: reports also go to it directly
static bool allowedMine[2] = {false, false};
static bool lostRaised = false;
static uint32_t lastMemberTx = 0;

static bool isLeader() { return !turns.empty(); }
static bool isMember() { return !isLeader() && cfg.seqLeader.length() && cfg.seqMask; }

static bool parseEntry(const char* s, Entry& e) {
  const char* c = strrchr(s, ':');
  if (!c || c == s || strncmp(s, "WM2_", 4) || (c[1] != '1' && c[1] != '2') || c[2]) return false;
  e.dev = String(s).substring(0, c - s);
  e.v = c[1] - '1';
  return true;
}

static struct Member* findMember(const String& dev, bool add);

static void reparse() {
  if (parsedFrom == cfg.seqTurns) return;
  parsedFrom = cfg.seqTurns;
  turns.clear();
  curTurn = -1;
  if (!cfg.seqTurns.length()) return;
  JsonDocument d;
  if (deserializeJson(d, cfg.seqTurns)) return;
  for (JsonArrayConst t : d.as<JsonArrayConst>()) {
    std::vector<Entry> turn;
    for (JsonVariantConst x : t) {
      Entry e;
      if (parseEntry(x | "", e) && turn.size() < SEQ_MAX_PER_TURN) turn.push_back(e);
    }
    if (!turn.empty() && turns.size() < SEQ_MAX_TURNS) turns.push_back(turn);
  }
  // Addresses the app gave for the listed nodes: reach them directly from the
  // start, even where the router does not pass broadcasts between WiFi clients.
  if (cfg.seqIps.length()) {
    JsonDocument ips;
    if (!deserializeJson(ips, cfg.seqIps))
      for (JsonPairConst kv : ips.as<JsonObjectConst>()) {
        IPAddress ip;
        if (ip.fromString(kv.value() | "")) {
          Member* m = findMember(kv.key().c_str(), true);
          if (!(uint32_t)m->ip) m->ip = ip;
        }
      }
  }
}

static struct Member* findMember(const String& dev, bool add) {
  for (auto& m : members)
    if (m.dev == dev) return &m;
  if (!add) return nullptr;
  members.push_back(Member());
  members.back().dev = dev;
  return &members.back();
}

static void sendTo(const IPAddress& to, const String& s) {
  bool ok = udp.beginPacket(to, PORT);
  udp.print(s);
  ok = udp.endPacket() && ok;
  if (ok) txOk++;
  else txErr++;
}

// Broadcast (to find each other), plus directly to every peer whose address
// is known: some routers do not pass a WiFi client's broadcasts on to the
// other clients, but always pass packets addressed to one of them.
static void send(JsonDocument& d) {
  if (!udpOn) return;
  String s;
  serializeJson(d, s);
  sendTo(WiFi.broadcastIP(), s);
  if (isLeader()) {
    for (auto& m : members)
      if ((uint32_t)m.ip) sendTo(m.ip, s);
  } else if ((uint32_t)leaderIp) {
    sendTo(leaderIp, s);
  }
}

static void zoneEvent(const char* what, int turn) {
  JsonDocument d;
  d["event"] = "zone";
  d["what"] = what;
  if (turn >= 0) d["turn"] = turn;
  emitEvent(d);
}

// ---- leader ---------------------------------------------------------------
static void leaderDecide(uint32_t now) {
  String me = cfg.deviceId();
  uint32_t uptimeS = now / 1000;
  uint8_t counts[SEQ_MAX_TURNS] = {};
  for (size_t t = 0; t < turns.size(); t++) {
    counts[t] = turns[t].size();
    for (size_t k = 0; k < turns[t].size(); k++) {
      const Entry& e = turns[t][k];
      if (e.dev == me) {
        const ValveState& s = core.valve(e.v);
        st[t][k] = seqEntryState(true, s.due, s.done, SEQ_NEVER, uptimeS);
      } else {
        Member* m = findMember(e.dev, false);
        uint32_t ago = m && m->heardMs ? (now - m->heardMs) / 1000 : SEQ_NEVER;
        st[t][k] = seqEntryState(false, m && m->due[e.v], m && m->done[e.v], ago, uptimeS);
      }
    }
  }
  int t = seqCurrentTurn(st, counts, turns.size());
  if (t != curTurn) {
    curTurn = t;
    zoneEvent(t < 0 ? "all_turns_done" : "turn", t + 1);
    stateChanged();
  }
}

static bool leaderAllows(const String& dev, int v) {
  if (!water || curTurn < 0) return false;
  for (const Entry& e : turns[curTurn])
    if (e.v == v && e.dev == dev) return true;
  return false;
}

static void leaderBroadcast() {
  JsonDocument d;
  d["t"] = "wm2z";
  d["ld"] = cfg.deviceId();
  d["w"] = water ? 1 : 0;
  d["turn"] = curTurn + 1;
  JsonArray al = d["al"].to<JsonArray>();
  JsonArray tu = d["tu"].to<JsonArray>();
  for (size_t t = 0; t < turns.size(); t++) {
    JsonArray a = tu.add<JsonArray>();
    for (const Entry& e : turns[t]) {
      String id = e.dev + ":" + (e.v + 1);
      a.add(id);
      if ((int)t == curTurn && water) al.add(id);
    }
  }
  send(d);
}

// ---- member ---------------------------------------------------------------
static void memberReport() {
  JsonDocument d;
  d["t"] = "wm2m";
  d["dev"] = cfg.deviceId();
  d["ld"] = cfg.seqLeader;
  JsonArray va = d["v"].to<JsonArray>();
  for (int v = 0; v < 2; v++) {
    const ValveState& s = core.valve(v);
    JsonArray a = va.add<JsonArray>();
    a.add(s.due ? 1 : 0);
    a.add(s.done ? 1 : 0);
    a.add(hw::valveState(v) ? 1 : 0);
  }
  send(d);
}

static void onLeaderPacket(JsonDocument& d, uint32_t now, const IPAddress& from) {
  if (isLeader()) return;  // a leader does not follow another leader
  String ld = d["ld"] | "";
  String me = cfg.deviceId();
  uint8_t mask = 0;
  for (JsonArrayConst t : d["tu"].as<JsonArrayConst>())
    for (JsonVariantConst x : t) {
      Entry e;
      if (parseEntry(x | "", e) && e.dev == me) mask |= 1 << e.v;
    }
  if (!mask) {
    // This leader no longer lists us: leave it (another leader may list us).
    if (cfg.seqLeader == ld) leave();
    return;
  }
  if (cfg.seqLeader.length() && cfg.seqLeader != ld && now - leaderHeardMs < LEADER_LOST_MS) return;  // keep the first
  if (cfg.seqLeader != ld || cfg.seqMask != mask) {
    bool fresh = cfg.seqLeader != ld;
    cfg.seqLeader = ld;
    cfg.seqMask = mask;
    cfg.save();
    if (fresh) zoneEvent("joined", -1);
    stateChanged();
  }
  bool was[2] = {allowedMine[0], allowedMine[1]};
  allowedMine[0] = allowedMine[1] = false;
  for (JsonVariantConst x : d["al"].as<JsonArrayConst>()) {
    Entry e;
    if (parseEntry(x | "", e) && e.dev == me) allowedMine[e.v] = true;
  }
  if (lostRaised) {
    lostRaised = false;
    zoneEvent("leader_back", -1);
  }
  if (leaderHeardMs == 0 || was[0] != allowedMine[0] || was[1] != allowedMine[1]) {
    memberReport();  // answer quickly on first contact and on a turn change
    lastMemberTx = now;
    stateChanged();
  }
  leaderHeardMs = now;
  leaderIp = from;
}

static void onMemberPacket(JsonDocument& d, uint32_t now, const IPAddress& from) {
  if (!isLeader() || cfg.deviceId() != (d["ld"] | "")) return;
  String dev = d["dev"] | "";
  bool listed = false;
  for (auto& t : turns)
    for (auto& e : t)
      if (e.dev == dev) listed = true;
  if (!listed) return;
  Member* m = findMember(dev, true);
  m->heardMs = now;
  m->ip = from;
  JsonArrayConst va = d["v"];
  for (int v = 0; v < 2 && v < (int)va.size(); v++) {
    m->due[v] = va[v][0] | 0;
    m->done[v] = va[v][1] | 0;
    m->open[v] = va[v][2] | 0;
  }
}

// ---- public ---------------------------------------------------------------
void gate(Inputs& in) {
  reparse();
  if (isLeader()) {
    String me = cfg.deviceId();
    for (auto& t : turns)
      for (auto& e : t)
        if (e.dev == me) in.turnGated[e.v] = true;
    for (int v = 0; v < 2; v++)
      if (in.turnGated[v]) in.turnAllowed[v] = leaderAllows(me, v);
  } else if (isMember()) {
    bool heard = leaderHeardMs && millis() - leaderHeardMs < LEADER_LOST_MS;
    for (int v = 0; v < 2; v++) {
      in.turnGated[v] = cfg.seqMask & (1 << v);
      in.turnAllowed[v] = in.turnGated[v] && heard && allowedMine[v];
    }
  }
}

void loop() {
  uint32_t now = millis();
  reparse();
  if (!udpOn && net::wifiUp()) udpOn = udp.begin(PORT);

  // receive
  if (udpOn) {
    for (int n = 0; n < 8; n++) {
      int len = udp.parsePacket();
      if (len <= 0) break;
      IPAddress from = udp.remoteIP();
      if (len > 1400) {
        udp.flush();
        continue;
      }
      char buf[1401];
      int r = udp.read(buf, sizeof buf - 1);
      buf[r > 0 ? r : 0] = 0;
      JsonDocument d;
      if (deserializeJson(d, buf)) continue;
      rxN++;
      const char* t = d["t"] | "";
      if (!strcmp(t, "wm2z") && cfg.deviceId() != (d["ld"] | "")) onLeaderPacket(d, now, from);
      else if (!strcmp(t, "wm2m")) onMemberPacket(d, now, from);
    }
  }

  if (isLeader()) {
    water = core.water() || bypassOn();
    leaderDecide(now);
    for (size_t i = 0; i < members.size();)
      if (members[i].heardMs && now - members[i].heardMs > MEMBER_KEEP_MS) members.erase(members.begin() + i);
      else i++;
    if (now - lastLeaderTx >= 2000) {
      lastLeaderTx = now;
      leaderBroadcast();
    }
  } else if (isMember()) {
    if (now - lastMemberTx >= 3000) {
      lastMemberTx = now;
      memberReport();
    }
    bool silent = leaderHeardMs ? now - leaderHeardMs >= LEADER_LOST_MS : now >= LEADER_LOST_MS;
    if (silent && !lostRaised) {
      lostRaised = true;
      allowedMine[0] = allowedMine[1] = false;
      zoneEvent("leader_lost", -1);
      stateChanged();
    }
  }
}

bool setTurns(JsonVariantConst in, JsonVariantConst ips, String& err) {
  if (!in.is<JsonArrayConst>()) {
    err = "turns must be an array of arrays";
    return false;
  }
  JsonArrayConst a = in.as<JsonArrayConst>();
  if (a.size() > SEQ_MAX_TURNS) {
    err = "at most 8 turns";
    return false;
  }
  JsonDocument out;
  JsonArray o = out.to<JsonArray>();
  std::vector<String> seen;
  for (JsonVariantConst t : a) {
    if (!t.is<JsonArrayConst>() || t.size() == 0 || t.size() > SEQ_MAX_PER_TURN) {
      err = "each turn: 1 to 8 valves";
      return false;
    }
    JsonArray ot = o.add<JsonArray>();
    for (JsonVariantConst x : t.as<JsonArrayConst>()) {
      Entry e;
      String id = x | "";
      if (!parseEntry(id.c_str(), e)) {
        err = "bad valve '" + id + "' (use WM2_XXXXXXXX:1 or :2)";
        return false;
      }
      for (auto& s : seen)
        if (s == id) {
          err = id + " is listed twice";
          return false;
        }
      seen.push_back(id);
      ot.add(id);
    }
  }
  members.clear();
  cfg.seqIps = "";
  if (o.size() && ips.is<JsonObjectConst>() && ips.size()) serializeJson(ips, cfg.seqIps);
  cfg.seqTurns = "";
  if (o.size()) serializeJson(o, cfg.seqTurns);
  if (o.size()) {  // a leader is not a member of anyone
    cfg.seqLeader = "";
    cfg.seqMask = 0;
  }
  parsedFrom = "\x01";  // re-read turns and addresses
  reparse();
  lastLeaderTx = 0;  // tell the members now
  stateChanged();
  return true;
}

void leave() {
  if (!cfg.seqLeader.length()) return;
  cfg.seqLeader = "";
  cfg.seqMask = 0;
  cfg.save();
  allowedMine[0] = allowedMine[1] = false;
  leaderHeardMs = 0;
  lostRaised = false;
  zoneEvent("left", -1);
  stateChanged();
}

static const char* stName(uint8_t s, bool open) {
  return s == SEQ_LOST ? "lost" : s == SEQ_FINISHED ? "finished" : open ? "running" : "waiting";
}

void toJson(JsonObject o) {
  reparse();
  uint32_t now = millis();
  if (isLeader()) {
    JsonObject z = o["zone"].to<JsonObject>();
    z["role"] = "leader";
    z["turn"] = curTurn + 1;  // 0 = all turns done today
    z["water"] = water;
    z["wifi"] = udpOn;
    z["udp"] = String(txOk) + " sent, " + txErr + " failed, " + rxN + " received, to " + WiFi.broadcastIP().toString();
    String me = cfg.deviceId();
    JsonArray ta = z["turns"].to<JsonArray>();
    for (size_t t = 0; t < turns.size(); t++) {
      JsonArray a = ta.add<JsonArray>();
      for (size_t k = 0; k < turns[t].size(); k++) {
        const Entry& e = turns[t][k];
        JsonObject x = a.add<JsonObject>();
        x["id"] = e.dev + ":" + (e.v + 1);
        bool open;
        if (e.dev == me) {
          open = hw::valveState(e.v);
        } else {
          Member* m = findMember(e.dev, false);
          open = m && m->open[e.v];
          x["heard_s"] = m && m->heardMs ? (int32_t)((now - m->heardMs) / 1000) : -1;
        }
        x["st"] = stName(st[t][k], open);
      }
    }
  } else if (cfg.seqLeader.length()) {
    JsonObject z = o["zone"].to<JsonObject>();
    z["role"] = "member";
    z["leader"] = cfg.seqLeader;
    z["heard_s"] = leaderHeardMs ? (int32_t)((now - leaderHeardMs) / 1000) : -1;
    z["lost"] = lostRaised;
    JsonArray g = z["gated"].to<JsonArray>();
    JsonArray al = z["allowed"].to<JsonArray>();
    for (int v = 0; v < 2; v++)
      if (cfg.seqMask & (1 << v)) {
        g.add(v + 1);
        if (allowedMine[v] && !lostRaised) al.add(v + 1);
      }
  }
}

}  // namespace zone
