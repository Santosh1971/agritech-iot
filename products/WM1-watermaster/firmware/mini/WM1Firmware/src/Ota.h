#pragma once
// Firmware update over WiFi, two ways:
//
//   push  POST /api/ota on the local server, body = firmware.bin, HTTP basic
//         auth ota:<OTA_PASS>, header X-MD5 required. For a PC on the same
//         WiFi as the board:  python3 tools/wmm_ota.py <board-ip>
//         One upload at a time; the MD5 must match before the board switches.
//
//   pull  command {"cmd":"ota_url","url":"...","sha256":"<64 hex>","mac":"<64 hex>"}
//         from the app, MQTT or serial; the board downloads the image itself,
//         so it works from anywhere through the cloud (tools/wmm_cloud.py ota).
//         The password is never sent: mac = HMAC-SHA256(OTA_PASS,
//         nonce + "|" + url + "|" + sha256), where nonce is the one-time value
//         the board shows as "ota_nonce" in its status. The nonce changes after
//         every correctly signed command, so it cannot be replayed,
//         and the image must hash to the signed sha256 or it is not used
//         (a changed file or a man in the middle cannot install other code).
//
// The image goes to the spare OTA slot; the board switches only after the
// whole image arrived and was checked, so an interrupted or wrong download
// leaves the running firmware in place.
// OTA_PASS is compiled in by wmm_secrets.py (env WMM_OTA_PASS or
// ~/.config/agrisense/wmm_ota_pass), never stored in this public repo.
// Without it both ways are disabled.

#include <Arduino.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#ifndef OTA_PASS
#define OTA_PASS ""
#endif

namespace ota {

inline bool enabled() { return OTA_PASS[0] != 0; }

// Last result, reported in the status JSON as "ota".
inline String& lastResult() { static String s; return s; }

inline String toHex(const uint8_t* b, size_t n) {
  static const char* h = "0123456789abcdef";
  String s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s += h[b[i] >> 4]; s += h[b[i] & 15]; }
  return s;
}

inline bool isHex(const String& s, size_t len) {
  if (s.length() != len) return false;
  for (size_t i = 0; i < len; i++) if (!isxdigit((unsigned char)s[i])) return false;
  return true;
}

// One-time value for the next pull command; new after every attempt.
inline String& nonce() {
  static String n;
  if (!n.length()) {
    uint8_t r[12];
    for (auto& x : r) x = (uint8_t)esp_random();
    n = toHex(r, sizeof(r));
  }
  return n;
}
inline void newNonce() { nonce() = ""; nonce(); }

inline String hmacHex(const String& msg) {
  uint8_t out[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, (const uint8_t*)OTA_PASS, strlen(OTA_PASS),
                  (const uint8_t*)msg.c_str(), msg.length(), out);
  return toHex(out, sizeof(out));
}

// Checks a pull command. The nonce is used up only by a correctly signed
// command (forging a signature is not feasible, so unsigned junk sent by
// anyone with the shared broker login cannot keep rotating it and block a
// real update); a used nonce cannot be replayed.
inline bool commandOk(const String& url, const String& sha256, const String& mac, String& err) {
  String n = nonce();
  if (!enabled()) { err = "ota_disabled"; return false; }
  if (!isHex(sha256, 64) || !isHex(mac, 64)) { err = "need sha256 and mac"; return false; }
  String want = hmacHex(n + "|" + url + "|" + sha256);
  String got = mac;
  got.toLowerCase();
  uint8_t diff = 0;   // constant-time compare
  for (size_t i = 0; i < 64; i++) diff |= (uint8_t)(want[i] ^ got[i]);
  if (diff) { err = "bad_mac"; return false; }
  newNonce();
  return true;
}

// Pull update. Blocking (tens of seconds); call from loop(), never from a
// network callback. Feeds the task watchdog while it downloads. On success
// it restarts the board and does not return.
inline bool pull(const String& url, const String& sha256Hex, String& err) {
  if (!enabled()) { err = "update not enabled in this build"; return false; }
  bool https = url.startsWith("https://");
  if (!https && !url.startsWith("http://")) { err = "url must start with http:// or https://"; return false; }

  WiFiClient plain;
  WiFiClientSecure secure;
  // The image is accepted only if it hashes to the signed sha256, so the
  // server certificate is not what protects it.
  secure.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  if (!http.begin(https ? (WiFiClient&)secure : plain, url)) { err = "bad url"; return false; }
  int code = http.GET();
  if (code != 200) { err = "download HTTP " + String(code); http.end(); return false; }
  int total = http.getSize();
  if (total <= 0) { err = "no content length"; http.end(); return false; }

  Serial.printf("[ota] downloading %d bytes from %s\n", total, url.c_str());
  if (Update.isRunning()) Update.abort();
  if (!Update.begin(total, U_FLASH)) { err = String("begin failed: ") + Update.errorString(); http.end(); return false; }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

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
        mbedtls_sha256_update(&sha, buf, n);
        if (Update.write(buf, n) != (size_t)n) { err = String("write failed: ") + Update.errorString(); break; }
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
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (!err.length()) {
    String got = toHex(digest, sizeof(digest));
    String want = sha256Hex;
    want.toLowerCase();
    if (got != want) err = "sha256 does not match the signed value";
  }
  if (err.length()) { Update.abort(); return false; }
  if (!Update.end(true)) { err = Update.errorString(); return false; }
  Serial.println("[ota] image downloaded and checked; restarting into it");
  delay(300);
  ESP.restart();
  return true;
}

}  // namespace ota
