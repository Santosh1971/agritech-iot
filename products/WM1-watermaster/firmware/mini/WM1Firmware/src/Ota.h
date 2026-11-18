#pragma once
// Firmware update over WiFi, two ways (same idea and safety as WM2 0.2.7):
//
//   push  POST /api/ota on the local server, body = firmware.bin, HTTP basic
//         auth ota:<OTA_PASS>, optional header X-MD5. For a PC on the same
//         WiFi as the board:  python3 tools/wmm_ota.py <board-ip>
//
//   pull  command {"cmd":"ota_url","url":"https://...bin","md5":"...","key":"<OTA_PASS>"}
//         from the app, MQTT or serial. The board downloads the image itself,
//         so it works from anywhere through the cloud (developer in Delhi,
//         board in Bangalore).
//
// Either way the image goes to the spare OTA slot and the board switches only
// after the whole image arrived and (when given) its MD5 matched, so an
// interrupted or corrupt download leaves the running firmware in place.
// OTA_PASS is compiled in by wmm_secrets.py (env WMM_OTA_PASS or
// ~/.config/agrisense/wmm_ota_pass), never stored in this public repo.
// Without it both ways are disabled.

#include <Arduino.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>

#ifndef OTA_PASS
#define OTA_PASS ""
#endif

namespace ota {

inline bool enabled() { return OTA_PASS[0] != 0; }

inline bool keyOk(const String& key) { return enabled() && key == OTA_PASS; }

// Last result, reported in the status JSON as "ota" so the app / cloud can
// see why a pull update did not happen.
inline String& lastResult() { static String s; return s; }

// Pull update. Blocking (tens of seconds); call from loop(), never from a
// network callback. Feeds the task watchdog while it downloads. On success
// it restarts the board and does not return.
inline bool pull(const String& url, const String& md5, String& err) {
  if (!enabled()) { err = "update not enabled in this build"; return false; }
  bool https = url.startsWith("https://");
  if (!https && !url.startsWith("http://")) { err = "url must start with http:// or https://"; return false; }

  WiFiClient plain;
  WiFiClientSecure secure;
  secure.setInsecure();   // firmware is checked by MD5, not by the server certificate
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  if (!http.begin(https ? (WiFiClient&)secure : plain, url)) { err = "bad url"; return false; }
  int code = http.GET();
  if (code != 200) { err = "download HTTP " + String(code); http.end(); return false; }
  int total = http.getSize();
  if (total <= 0) { err = "no content length"; http.end(); return false; }

  Serial.printf("[ota] downloading %d bytes from %s\n", total, url.c_str());
  if (!Update.begin(total, U_FLASH)) { err = Update.errorString(); http.end(); return false; }
  if (md5.length() == 32) Update.setMD5(md5.c_str());

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  int done = 0;
  uint32_t lastData = millis();
  while (done < total) {
    esp_task_wdt_reset();
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (n > 0) {
        if (Update.write(buf, n) != (size_t)n) { err = Update.errorString(); break; }
        done += n;
        lastData = millis();
      }
    } else if (!http.connected() || millis() - lastData > 20000) {
      err = "download stopped at " + String(done) + " of " + String(total) + " bytes";
      break;
    } else {
      delay(5);
    }
  }
  http.end();
  if (err.length()) { Update.abort(); return false; }
  if (!Update.end(true)) { err = Update.errorString(); return false; }
  Serial.println("[ota] image downloaded and checked; restarting into it");
  delay(300);
  ESP.restart();
  return true;
}

}  // namespace ota
