#include "web.h"

#include <ESPAsyncWebServer.h>
#include <Update.h>

#include <memory>

#include "app.h"
#include "commands.h"
#include "hw.h"
#include "history.h"
#include "settings.h"
#include "webpage.h"

// Firmware update over WiFi (0.2.7). Password compiled in from WM2_OTA_PASS by
// wm2_secrets.py (never in the repo); without it the endpoint is disabled.
#ifndef WM2_OTA_PASS
#define WM2_OTA_PASS ""
#endif

namespace web {

static bool otaOk = false;
static String otaErr;

bool otaEnabled() { return WM2_OTA_PASS[0] != 0; }

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

int clients() { return ws.count(); }

void broadcast(const String& json) {
  if (ws.count()) ws.textAll(json);
}

void begin() {
  ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data,
                size_t len) {
    if (type == WS_EVT_CONNECT) {
      JsonDocument d;
      d["type"] = "state";
      {
        Lock lock;
        buildState(d["data"].to<JsonObject>());
      }
      String s;
      serializeJson(d, s);
      client->text(s);
    } else if (type == WS_EVT_DATA) {
      AwsFrameInfo* info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        String msg((const char*)data, len);
        client->text(dispatchCommand(msg, "app"));
      }
    }
  });
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", PAGE_HTML);
  });

  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument d;
    {
      Lock lock;
      buildState(d.to<JsonObject>());
    }
    String s;
    serializeJson(d, s);
    req->send(200, "application/json", s);
  });

  server.on(
      "/api/cmd", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr,
      [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) {
          body = "";
          body.reserve(total);
        }
        body.concat((const char*)data, len);
        if (index + len == total) req->send(200, "application/json", dispatchCommand(body, "http"));
      });

  server.on("/api/history.csv", HTTP_GET, [](AsyncWebServerRequest* req) {
    uint32_t to = req->hasParam("to") ? req->getParam("to")->value().toInt() : nowUtc();
    uint32_t from = req->hasParam("from") ? req->getParam("from")->value().toInt() : (to > 86400 ? to - 86400 : 0);
    auto cur = std::make_shared<history::CsvCursor>();
    history::csvBegin(*cur, from, to, cfg.tzMin);
    AsyncWebServerResponse* r = req->beginChunkedResponse("text/csv", [cur](uint8_t* buf, size_t max, size_t) -> size_t {
      Lock lock;
      return history::csvChunk(*cur, buf, max > 1024 ? 1024 : max);
    });
    r->addHeader("Content-Disposition", "attachment; filename=\"" + cfg.hostname() + "-history.csv\"");
    req->send(r);
  });

  // POST /api/ota, body = firmware.bin, HTTP basic auth wm2:<WM2_OTA_PASS>,
  // optional header X-MD5. The image goes to the spare OTA slot; the node
  // only switches to it after the whole image arrived and checked out, so an
  // interrupted or bad upload leaves the running firmware untouched.
  server.on(
      "/api/ota", HTTP_POST,
      [](AsyncWebServerRequest* req) {
        if (!otaEnabled()) return req->send(403, "application/json", "{\"ok\":false,\"err\":\"WiFi update not enabled in this build\"}");
        if (!req->authenticate("wm2", WM2_OTA_PASS)) return req->requestAuthentication();
        if (otaOk) {
          req->send(200, "application/json", "{\"ok\":true,\"msg\":\"updated, restarting\"}");
          requestReboot(1500);
        } else {
          req->send(500, "application/json", "{\"ok\":false,\"err\":\"" + otaErr + "\"}");
        }
      },
      nullptr,
      [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) {
          otaOk = false;
          otaErr = "";
          if (!otaEnabled() || !req->authenticate("wm2", WM2_OTA_PASS)) {
            otaErr = "not authorised";
            return;
          }
          if (!Update.begin(total, U_FLASH)) {
            otaErr = Update.errorString();
            return;
          }
          if (req->hasHeader("X-MD5")) Update.setMD5(req->header("X-MD5").c_str());
          Serial.printf("[ota] receiving %u bytes over WiFi\n", (unsigned)total);
          hw::ledsIdentify(180);  // all LEDs blink while the update comes in
        }
        if (otaErr.length()) return;
        if (Update.write(data, len) != len) {
          otaErr = Update.errorString();
          Update.abort();
          hw::ledsIdentify(0);
          Serial.printf("[ota] failed: %s\n", otaErr.c_str());
          return;
        }
        if (index + len == total) {
          if (Update.end(true)) {
            otaOk = true;
            Serial.println("[ota] image received and checked; restarting into it");
          } else {
            otaErr = Update.errorString();
            Serial.printf("[ota] failed: %s\n", otaErr.c_str());
          }
          hw::ledsIdentify(0);
        }
      });

  server.onNotFound([](AsyncWebServerRequest* req) {
    // SoftAP users typing any address land on the page.
    req->redirect("/");
  });

  server.begin();
}

void loop() { ws.cleanupClients(4); }

}  // namespace web
