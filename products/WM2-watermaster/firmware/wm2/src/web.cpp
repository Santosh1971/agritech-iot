#include "web.h"

#include <ESPAsyncWebServer.h>

#include <memory>

#include "app.h"
#include "commands.h"
#include "history.h"
#include "settings.h"
#include "webpage.h"

namespace web {

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

  server.onNotFound([](AsyncWebServerRequest* req) {
    // SoftAP users typing any address land on the page.
    req->redirect("/");
  });

  server.begin();
}

void loop() { ws.cleanupClients(4); }

}  // namespace web
