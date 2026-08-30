#pragma once
#include <ESPAsyncWebServer.h>
#include "Config.h"
#include "CommandHandler.h"

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
      String msg((char*)data, len);
      if (_handler) {
        _handler->handle(msg, [client](const String& reply) {
          client->text(reply);
        });
      }
    }
  }

  AsyncWebServer _server{LOCAL_SERVER_PORT};
  AsyncWebSocket _ws{"/ws"};
  CommandHandler* _handler = nullptr;
};
