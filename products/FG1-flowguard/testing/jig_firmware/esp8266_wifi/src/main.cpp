/*
 * FG1 Test Jig Controller — ESP8266, WiFi HTTP version
 *
 * Runs on a small permanent fixture MCU (Wemos D1 Mini / any ESP8266
 * board) — NOT the DUT. Implements the HTTP API in
 * docs/testing/PRODUCTION_TOOL_SPEC.md section 6.3, called directly by
 * the phone app once it's joined the DUT's own SoftAP.
 *
 * Behavior:
 *   1. On boot (and whenever not currently joined to a DUT), scans for
 *      WiFi networks matching what it's supposed to join -- see
 *      "Targeting" below.
 *   2. Joins using JOIN_PASSWORD (the DUT's fixed SOFTAP_PASSWORD from
 *      firmware/include/Config.h — see the PRODUCTION_TOOL_SPEC.md
 *      section 9.1 note: this only works while that password stays a
 *      single known fixed value across units).
 *   3. Advertises itself as fg1jig.local (mDNS) and serves a tiny HTTP
 *      API for pulse injection + relay sensing.
 *   4. If the joined network disappears (unit swapped/unplugged), goes
 *      back to scanning.
 *
 * Targeting -- which DUT to join when more than one might be powered
 * nearby (see PRODUCTION_TOOL_SPEC.md section 6.4):
 *   - If this jig is ALSO connected to the bench laptop via USB
 *     (recommended), flash_bridge.py sends a serial command the moment
 *     it parses a freshly-flashed DUT's device ID from its boot log:
 *     `TARGET:<ssid>\n` -- the jig then joins that EXACT SSID only,
 *     ignoring every other unit's SoftAP even if one has a stronger
 *     signal. This is the reliable mode -- use it whenever possible.
 *   - If not connected to a laptop (jig running on its own, e.g. an
 *     older/simpler bench), it falls back to scanning for any SSID
 *     starting with TARGET_SSID_PREFIX and joining the strongest
 *     match -- ambiguous if more than one DUT is powered at once, so
 *     in that mode only power one DUT near the jig at a time.
 *
 * Wiring:
 *   PULSE_OUT_PIN   (D1 / GPIO5)  -> jumper into DUT's flow sensor input (GPIO35)
 *   RELAY_SENSE_PIN (D2 / GPIO4)  -> relay's own dry contact: COM to jig
 *                                    GND, NO to this pin, read with an
 *                                    internal pull-up -- no external
 *                                    voltage or divider needed (see
 *                                    PRODUCTION_TOOL_SPEC.md section 6.1;
 *                                    supersedes the resistor-divider
 *                                    design originally drafted in
 *                                    TEST_JIG_SPEC.md section 2.4).
 *   Common GND between the jig and DUT boards is required regardless --
 *   without it the pulse signal won't register at the DUT's GPIO35 at
 *   all, even though it looks correctly wired.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

// ---------- Config — tune for your bench ----------
const char *TARGET_SSID_PREFIX = "SWC_";       // DUT SoftAP SSIDs look like "SWC_001_A1B2"
const char *JOIN_PASSWORD      = "water1234";  // must match firmware's SOFTAP_PASSWORD (Config.h)
const char *MDNS_NAME          = "fg1jig";     // mDNS is kept for laptop-side debugging
                                                // (macOS/Linux resolve .local natively), but
                                                // NOT relied on by the phone app -- stock
                                                // Android does not resolve mDNS hostnames via
                                                // plain HTTP clients (needs the NSD API, which
                                                // Dart's http package doesn't use), so the app
                                                // is configured to hit the static IP below.

// The DUT's SoftAP is always 192.168.4.0/24 with the DUT itself as
// .1 (ESP32 SoftAP default, see firmware's startLocalFallback()) --
// so a fixed static IP here is reliable across every unit, unlike a
// DHCP-assigned address which depends on join order.
IPAddress JIG_STATIC_IP(192, 168, 4, 50);
IPAddress JIG_GATEWAY(192, 168, 4, 1);
IPAddress JIG_SUBNET(255, 255, 255, 0);

const int PULSE_OUT_PIN   = 5;   // D1
// Relay sense: a dry contact off the relay's own mechanical switch --
// COM to jig GND, NO to this pin -- read with an internal pull-up, no
// external voltage/divider needed at all (simpler and safer than the
// original resistor-divider design in TEST_JIG_SPEC.md section 2.4,
// which assumed a powered "relay output" node and needs re-deriving
// per supply voltage to avoid exceeding A0's ~3.2V limit). Relay OFF
// (NO open) reads HIGH via the pull-up; relay ON (contacts closed to
// GND) reads LOW. D2 chosen over D3/D4/D8 since those have boot-time
// strapping requirements that a relay contact's power-on state could
// interfere with.
const int RELAY_SENSE_PIN = 4;   // D2
const int STATUS_LED_PIN  = LED_BUILTIN;  // active-low on most ESP8266 boards

// Pulse timing — matches the original serial-based jig's timing
// (fast enough for a quick test, slow enough to be a clean digital
// edge for the DUT's flow sensor interrupt).
const int PULSE_HIGH_US = 500;
const int PULSE_LOW_US  = 500;

const unsigned long SCAN_RETRY_INTERVAL_MS = 5000;
const unsigned long JOIN_TIMEOUT_MS        = 8000;

// ---------- State ----------
ESP8266WebServer server(80);
bool joined = false;
String joinedSsid = "";
unsigned long lastScanAttempt = 0;

// Set via a `TARGET:<ssid>` serial command from flash_bridge.py the
// moment it knows exactly which DUT was just flashed -- see the
// "Targeting" section in the header comment. Empty = not pinned, falls
// back to auto-scan-strongest-match against TARGET_SSID_PREFIX.
String pinnedTargetSsid = "";
String serialInputLine;

void setLed(bool on) {
  digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);  // active-low
}

void emitPulses(int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(PULSE_OUT_PIN, HIGH);
    delayMicroseconds(PULSE_HIGH_US);
    digitalWrite(PULSE_OUT_PIN, LOW);
    delayMicroseconds(PULSE_LOW_US);
  }
}

// Scans for a DUT SoftAP and attempts to join it. Blocking (a scan
// takes a few seconds) — acceptable since this only runs while the
// jig has no DUT to serve anyway.
void tryJoinDut() {
  bool pinned = pinnedTargetSsid.length() > 0;
  Serial.println(pinned
      ? "[jig] Scanning for pinned target " + pinnedTargetSsid + "..."
      : "[jig] Scanning for a DUT SoftAP...");
  int n = WiFi.scanNetworks();
  int bestIdx = -1;
  int bestRssi = -1000;
  String bestSsid;  // must be captured while the scan table is still
                     // alive -- WiFi.SSID(i) returns a reference into
                     // it, which scanDelete() below invalidates. Reading
                     // it again afterward (as this used to) silently
                     // returns an empty string, so the jig would try to
                     // join "" instead of the DUT's real SSID.
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    // Pinned: exact match only, ignoring every other unit's SoftAP
    // even at higher signal strength -- this is what actually resolves
    // the multi-unit-in-range ambiguity. Unpinned: fall back to the
    // old best-RSSI-among-any-prefix-match behavior.
    bool matches = pinned ? (ssid == pinnedTargetSsid) : ssid.startsWith(TARGET_SSID_PREFIX);
    if (matches) {
      int rssi = WiFi.RSSI(i);
      if (rssi > bestRssi) {
        bestRssi = rssi;
        bestIdx = i;
        bestSsid = ssid;
      }
    }
  }
  WiFi.scanDelete();

  if (bestIdx < 0) {
    Serial.println(pinned
        ? "[jig] Pinned target " + pinnedTargetSsid + " not found this pass."
        : "[jig] No DUT SoftAP found this pass.");
    return;
  }

  Serial.printf("[jig] Joining %s (rssi %d)...\n", bestSsid.c_str(), bestRssi);
  WiFi.config(JIG_STATIC_IP, JIG_GATEWAY, JIG_SUBNET);
  WiFi.begin(bestSsid.c_str(), JOIN_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < JOIN_TIMEOUT_MS) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    joined = true;
    joinedSsid = bestSsid;
    Serial.printf("[jig] Joined %s, IP %s\n", joinedSsid.c_str(), WiFi.localIP().toString().c_str());
    if (MDNS.begin(MDNS_NAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[jig] mDNS up: http://%s.local/\n", MDNS_NAME);
    } else {
      Serial.println("[jig] mDNS begin failed (app will need the IP directly)");
    }
  } else {
    Serial.println("[jig] Join attempt timed out.");
    WiFi.disconnect();
  }
}

// ---------- HTTP handlers ----------
void sendJson(int code, const String &body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", body);
}

void handlePing() {
  sendJson(200, "{\"ok\":true}");
}

void handleStatus() {
  String body = "{\"joined_ssid\":\"" + joinedSsid + "\",\"rssi\":" + String(WiFi.RSSI()) +
                ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  sendJson(200, body);
}

void handleRelay() {
  // Pull-up: open contact (relay OFF) reads HIGH, closed contact
  // (relay ON, NO shorted to jig GND) reads LOW.
  bool on = digitalRead(RELAY_SENSE_PIN) == LOW;
  sendJson(200, String("{\"state\":\"") + (on ? "on" : "off") + "\"}");
}

void handlePulse() {
  if (!server.hasArg("n")) {
    sendJson(400, "{\"ok\":false,\"error\":\"missing n\"}");
    return;
  }
  int count = server.arg("n").toInt();
  if (count <= 0) {
    sendJson(400, "{\"ok\":false,\"error\":\"bad count\"}");
    return;
  }
  emitPulses(count);
  sendJson(200, "{\"ok\":true,\"emitted\":" + String(count) + "}");
}

void handleNotFound() {
  sendJson(404, "{\"ok\":false,\"error\":\"not found\"}");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[jig] FG1 Test Jig Controller (ESP8266/WiFi) starting...");

  pinMode(PULSE_OUT_PIN, OUTPUT);
  digitalWrite(PULSE_OUT_PIN, LOW);
  pinMode(RELAY_SENSE_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);
  setLed(false);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  server.on("/ping", HTTP_GET, handlePing);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/relay", HTTP_GET, handleRelay);
  server.on("/pulse", HTTP_POST, handlePulse);
  server.on("/pulse", HTTP_GET, handlePulse);  // GET too — convenient for manual testing in a browser
  server.onNotFound(handleNotFound);
  server.begin();

  tryJoinDut();
  lastScanAttempt = millis();
}

// TARGET:<ssid> -- pin an exact SSID to join (see header comment's
// "Targeting" section). Sent by flash_bridge.py over USB the moment it
// parses a freshly-flashed DUT's device ID from its boot log.
void handleSerialCommand(const String &line) {
  if (line.startsWith("TARGET:")) {
    String newTarget = line.substring(7);
    newTarget.trim();
    bool changed = newTarget != pinnedTargetSsid;
    pinnedTargetSsid = newTarget;
    Serial.printf("OK:TARGET:%s\n", pinnedTargetSsid.c_str());
    // If we're already joined to a *different* unit, drop it now and
    // force an immediate re-scan toward the new target rather than
    // waiting up to SCAN_RETRY_INTERVAL_MS -- the operator is waiting
    // on this at the "join WiFi" screen.
    if (changed && joined && joinedSsid != pinnedTargetSsid) {
      Serial.println("[jig] Target changed — dropping current DUT connection.");
      WiFi.disconnect();
      joined = false;
      joinedSsid = "";
      lastScanAttempt = 0;
    }
    return;
  }
  Serial.println("ERR:unknown command");
}

void pollSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      serialInputLine.trim();
      if (serialInputLine.length() > 0) handleSerialCommand(serialInputLine);
      serialInputLine = "";
    } else if (c != '\r') {
      serialInputLine += c;
    }
  }
}

void loop() {
  pollSerialCommands();

  if (WiFi.status() != WL_CONNECTED) {
    if (joined) {
      Serial.println("[jig] Lost DUT connection.");
      joined = false;
      joinedSsid = "";
    }
    setLed(false);
    if (millis() - lastScanAttempt > SCAN_RETRY_INTERVAL_MS) {
      lastScanAttempt = millis();
      tryJoinDut();
    }
  } else {
    setLed(true);
    server.handleClient();
    MDNS.update();
  }
}
