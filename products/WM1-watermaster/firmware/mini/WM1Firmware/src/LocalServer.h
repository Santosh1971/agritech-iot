#pragma once
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "CommandHandler.h"
#include "Ota.h"

// AsyncTCP + ESPAsyncWebServer (ESP32Async forks — see platformio.ini).
//
// IMPORTANT: begin() must be called AFTER WiFi.mode() is set (done
// inside WiFiManager::begin()), or it triggers an "assert failed:
// tcpip_api_call ... Invalid mbox" reboot loop — main.cpp calls
// wifiManager.begin() before localServer.begin() accordingly.
class LocalServer {
public:
  void begin(CommandHandler* handler) {
    _handler = handler;

    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
      req->send(200, "text/plain", "Water Manager-Mini local server");
    });

    // Plain HTTP status read (same JSON as the "status" push), used by
    // tools/wmm_ota.py to see the version before and after an update.
    _server.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest* req) {
      req->send(200, "application/json", _handler->buildStatusJson());
    });

    // Push update: POST /api/ota, body = firmware.bin, basic auth
    // ota:<OTA_PASS>, optional X-MD5 header. See Ota.h.
    _server.on(
        "/api/ota", HTTP_POST,
        [](AsyncWebServerRequest* req) {
          if (!ota::enabled()) return req->send(403, "application/json", "{\"ok\":false,\"err\":\"update not enabled in this build\"}");
          if (!req->authenticate("ota", OTA_PASS)) return req->requestAuthentication();
          if (_pushOk) {
            req->send(200, "application/json", "{\"ok\":true,\"msg\":\"updated, restarting\"}");
            _restartAt = millis() + 800;
          } else {
            req->send(500, "application/json", "{\"ok\":false,\"err\":\"" + _pushErr + "\"}");
          }
        },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
          if (index == 0) {
            _pushOk = false;
            _pushErr = "";
            if (!ota::enabled() || !req->authenticate("ota", OTA_PASS)) { _pushErr = "not authorised"; return; }
            // An upload cut off earlier leaves Update running, and begin()
            // then fails with no error text: clear it first.
            if (Update.isRunning()) { Serial.println("[ota] clearing an unfinished update"); Update.abort(); }
            if (!Update.begin(total, U_FLASH)) {
              _pushErr = String("begin failed: ") + Update.errorString();
              Serial.printf("[ota] %s\n", _pushErr.c_str());
              return;
            }
            if (req->hasHeader("X-MD5")) Update.setMD5(req->header("X-MD5").c_str());
            Serial.printf("[ota] receiving %u bytes over WiFi\n", (unsigned)total);
            // If the PC goes away mid-upload, drop the half-written image.
            req->onDisconnect([]() {
              if (!_pushOk && Update.isRunning()) { Update.abort(); Serial.println("[ota] upload cut off; running firmware kept"); }
            });
          }
          if (_pushErr.length()) return;
          if (Update.write(data, len) != len) {
            _pushErr = String("write failed at ") + String((unsigned)index) + ": " + Update.errorString();
            Serial.printf("[ota] %s\n", _pushErr.c_str());
            Update.abort();
            return;
          }
          if ((index / 65536) != ((index + len) / 65536)) Serial.printf("[ota] %u%%\n", (unsigned)((index + len) * 100 / total));
          if (index + len == total) {
            if (Update.end(true)) {
              _pushOk = true;
              Serial.println("[ota] image received and checked; restarting into it");
            } else {
              _pushErr = Update.errorString();
              Serial.printf("[ota] failed: %s\n", _pushErr.c_str());
            }
          }
        });

    _ws.onEvent([this](AsyncWebSocket* ws, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
      _onWsEvent(ws, client, type, arg, data, len);
    });
    _server.addHandler(&_ws);
    _server.begin();
    Serial.printf("[LocalServer] Started on port %d\n", LOCAL_SERVER_PORT);
  }

  // Wraps already-serialized JSON (object/array) in a {"type","data"}
  // envelope and broadcasts to every connected WebSocket client — the
  // shape the app's LocalService expects for push updates (mirrors
  // FG1's local_service.dart _handleMessage 'type' branch).
  void broadcastTyped(const char* type, const String& innerJson) {
    String out = "{\"type\":\"";
    out += type;
    out += "\",\"data\":";
    out += innerJson;
    out += "}";
    _ws.textAll(out);
  }

  // Called every loop() iteration. cleanupClients() only prunes clients
  // the library itself already knows are gone; it does NOT detect a
  // client that vanished without a clean TCP close (e.g. a phone
  // leaving the SoftAP's WiFi range/network) — that kind of drop can
  // otherwise sit undetected for a long time (TCP retransmission
  // timeouts, not seconds), which is exactly why the status LED stayed
  // stuck on "connected" after the phone actually left. _checkHealth()
  // is the fix: an application-level ping/pong heartbeat that forcibly
  // closes any client that stops responding.
  void loop() {
    _ws.cleanupClients();
    if (millis() - _lastHealthCheck > HEALTH_CHECK_INTERVAL_MS) {
      _lastHealthCheck = millis();
      _checkHealth();
    }
  }

  // Actual connected-app signal for the status LED — more accurate than
  // WiFi station count, since a phone can join the AP's WiFi without
  // the app itself ever opening the WebSocket. Reflects reality within
  // one HEALTH_CHECK_INTERVAL_MS + PONG_TIMEOUT_MS of the client
  // actually vanishing, not an unbounded TCP-timeout delay.
  size_t clientCount() const { return _ws.count(); }

  // Restart after a successful push update, once its reply has gone out.
  void serviceOtaRestart() {
    if (_restartAt && (int32_t)(millis() - _restartAt) >= 0) ESP.restart();
  }

private:
  static constexpr uint32_t HEALTH_CHECK_INTERVAL_MS = 5000;
  static constexpr uint32_t PONG_TIMEOUT_MS = 12000;  // ~2.4 missed pings before we give up on a client
  static constexpr uint8_t MAX_TRACKED_CLIENTS = 4;

  struct ClientHealth {
    uint32_t id = 0;
    uint32_t lastPongMillis = 0;
    bool inUse = false;
  };
  ClientHealth _health[MAX_TRACKED_CLIENTS];
  uint32_t _lastHealthCheck = 0;

  ClientHealth* _findOrCreateHealth(uint32_t id) {
    for (auto& h : _health) if (h.inUse && h.id == id) return &h;
    for (auto& h : _health) if (!h.inUse) { h.inUse = true; h.id = id; h.lastPongMillis = millis(); return &h; }
    return nullptr;  // more than MAX_TRACKED_CLIENTS at once — not expected for this app
  }
  void _forgetHealth(uint32_t id) {
    for (auto& h : _health) if (h.inUse && h.id == id) { h.inUse = false; return; }
  }

  void _checkHealth() {
    for (auto& h : _health) {
      if (!h.inUse) continue;
      uint32_t silentFor = millis() - h.lastPongMillis;
      if (silentFor > PONG_TIMEOUT_MS) {
        Serial.printf("[LocalServer] Client %u silent for %ums — closing (likely left the network without a clean disconnect)\n",
                      h.id, silentFor);
        for (auto& c : _ws.getClients()) {
          if (c.id() == h.id) { c.close(); break; }
        }
        h.inUse = false;  // WS_EVT_DISCONNECT will also fire and no-op on an already-cleared entry
      } else {
        _ws.ping(h.id);
      }
    }
  }

  void _onWsEvent(AsyncWebSocket* ws, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      Serial.printf("[LocalServer] Client connected: %u\n", client->id());
      _findOrCreateHealth(client->id());
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.printf("[LocalServer] Client disconnected: %u\n", client->id());
      _forgetHealth(client->id());
    } else if (type == WS_EVT_PONG) {
      ClientHealth* h = _findOrCreateHealth(client->id());
      if (h) h->lastPongMillis = millis();
    } else if (type == WS_EVT_DATA) {
      // Bug fix: without checking the frame info, WS_EVT_DATA firing
      // more than once for what the client sent as a single logical
      // message (confirmed live — a single wifi_config send produced
      // TWO "Attempting STA connect" logs and the ESP-IDF driver's
      // "sta is connecting, return error", because setCredentials()
      // and its _startStaConnect() actually ran twice) gets dispatched
      // twice. FG1's real LocalServer.cpp already guards this exact
      // way — final/single-frame/text messages only.
      AwsFrameInfo* info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        String msg((char*)data, len);
        if (_handler) {
          _handler->handle(msg, [client](const String& reply) {
            client->text(reply);
          });
        }
      }
    }
  }

  AsyncWebServer _server{LOCAL_SERVER_PORT};
  AsyncWebSocket _ws{"/ws"};
  CommandHandler* _handler = nullptr;
  static inline bool _pushOk = false;
  static inline String _pushErr;
  static inline uint32_t _restartAt = 0;
};
