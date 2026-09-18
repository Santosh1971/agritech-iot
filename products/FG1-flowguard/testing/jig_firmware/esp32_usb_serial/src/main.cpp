/*
 * FG1 Test Jig Controller — ESP32, USB-serial version
 *
 * Runs on a small permanent fixture MCU (any ESP32 devkit) — NOT the
 * DUT. Command/control is wired to the SAME phone as the DUT via a
 * USB-OTG hub (see flasher-tester-app's MainActivity —
 * probeAndIdentifyPorts()), found deterministically by sending PING on
 * every connected USB-serial port and treating whichever one replies
 * PONG as the jig — nothing to configure per-unit, no targeting
 * ambiguity (that problem was specific to the earlier WiFi jig, see
 * jig_firmware/esp8266_wifi's header comment for the history).
 *
 * As of docs/testing/JIG_NETWORK_BRIDGE_SPEC.md, this board joins WiFi
 * networks and relays commands to the DUT on request — HTTP to the
 * DUT's local API while joined to its own SoftAP (Phase 1:
 * JOIN_AP/LEAVE_WIFI/WIFI_STATUS?/HTTP_CMD), and now also MQTT once the
 * DUT has been told to join the office WiFi (Phase 2:
 * MQTT_CONNECT/MQTT_CMD/MQTT_STATUS? below). The controlling app (phone
 * or laptop, see PRODUCTION_TOOL_SPEC_V2.md) never touches WiFi itself
 * at all now -- it only ever talks to *this board* over USB-serial, the
 * same way PING/RELAY?/PULSE:<n> always have.
 *
 * This revives the original serial protocol from
 * jig_firmware/serial_legacy/jig_controller.ino (written for an
 * Arduino Nano, before the brief ESP8266-WiFi detour), just on ESP32,
 * and with the dry-contact relay sense validated on the bench
 * 2026-09-07/08 (see esp8266_wifi/src/main.cpp's header comment)
 * instead of that draft's resistor divider.
 *
 * Wiring (matches the Tester PCB, pinned 2026-09-15; PULSE_OUT_PIN moved
 * 2026-09-18 -- see that pin's own comment below for why):
 *   PULSE_OUT_PIN   (GPIO23) -> jumper into DUT's flow sensor input (GPIO35)
 *   RELAY_SENSE_PIN (GPIO35) -> relay's own dry contact: active LOW
 *                                when the relay is ON. GPIO35 is one of
 *                                ESP32's input-only pins (34-39) -- no
 *                                internal pull-up exists on these at
 *                                all, so INPUT_PULLUP would be a no-op
 *                                here. This assumes the Tester PCB has
 *                                its own EXTERNAL pull-up on this line
 *                                (needed for a clean HIGH when the
 *                                relay is off/contact open) -- confirm
 *                                that resistor is actually populated;
 *                                without it this pin floats when idle.
 *   Common GND between the jig and DUT boards is required regardless
 *   of USB power -- without it the pulse signal won't register at the
 *   DUT's GPIO35 at all, even though it looks correctly wired (both
 *   boards' USB grounds are typically tied together through the hub
 *   anyway, but don't rely on that -- run an explicit jumper too).
 *
 * Pins are easily reassigned here if these are already spoken for on
 * your board -- just avoid ESP32's boot-strapping pins (0, 2, 5, 12,
 * 15), and remember the input-only pins (34-39) need an external
 * pull-up for RELAY_SENSE_PIN specifically (see above).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>

// 2026-09-18: moved off GPIO32 permanently -- this is very likely the
// actual root cause of the flow_sensor "0.00L" result chased across this
// entire project (Android app and this laptop CLI both, many iterations
// of software fixes, none of which touched this). Bench-proven, not
// guessed: a sustained 1Hz square wave (500ms high/low -- driven by the
// exact same code path as STATUS_LED_PIN, which blinks correctly)
// showed nothing on a multimeter at D32, while the identical code on
// GPIO23 showed a clean square wave. Same firmware, same timing, one
// pin dead -- that's a physically broken pin or trace on this specific
// Tester board, not a firmware bug, not a DUT-side fault, not a
// calibration issue. GPIO23 is a plain general-purpose pin (no
// input-only or boot-strapping constraints). The jumper into the DUT's
// GPIO35 needs to move from this board's D32 to its D23 to match.
const int PULSE_OUT_PIN   = 23;
const int RELAY_SENSE_PIN = 35;
const int STATUS_LED_PIN  = 2;  // onboard LED on most ESP32 devkits

void handleSquare(const String &args);  // defined below setup(); forward-declared for setup()'s own use

// Phase 1 of docs/testing/JIG_NETWORK_BRIDGE_SPEC.md -- SoftAP-phase only.
// The jig joins WiFi networks and relays HTTP commands to the DUT's local
// API on request, so the phone's own WiFi state stops mattering for any
// of this; the phone only ever talks to the jig over USB-serial, same as
// PING/RELAY?/PULSE:<n> always have. Office-WiFi/MQTT commands are still
// phone-side (MqttCommander) until that's migrated in a later phase.
//
// Blocking implementation deliberately, same simplicity as the rest of
// this sketch -- nothing else time-critical needs to run while an
// explicit network command is being serviced mid-test.
const uint32_t WIFI_JOIN_TIMEOUT_MS = 10000;
const uint32_t HTTP_TIMEOUT_MS      = 8000;

void handleJoinAp(const String &args) {
  // "<ssid>:<pass>" -- first colon splits; SSIDs/passwords containing a
  // literal colon aren't supported (none in use on this bench do).
  int sep = args.indexOf(':');
  if (sep < 0) {
    Serial.println("JOIN_FAIL:bad args");
    return;
  }
  String ssid = args.substring(0, sep);
  String pass = args.substring(sep + 1);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_JOIN_TIMEOUT_MS) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("JOINED:");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("JOIN_FAIL:timeout");
  }
}

void handleLeaveWifi() {
  WiFi.disconnect(true);
  Serial.println("OK");
}

void handleWifiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WIFI:connected:");
    Serial.print(WiFi.SSID());
    Serial.print(":");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WIFI:none::");
  }
}

// POSTs <json> to the DUT's local /command endpoint (LocalServer.cpp's
// plain-HTTP alternative to its WS API -- same {"cmd":...} schema, same
// _dispatch() handler on the DUT side) and relays the JSON response back
// verbatim. Only meaningful while joined to a DUT's own SoftAP
// (192.168.4.1 is always its gateway IP in that mode, same assumption
// DutWsClient makes phone-side).
void handleHttpCmd(const String &json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("HTTP_FAIL:not connected");
    return;
  }
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin("http://192.168.4.1/command");
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  if (code == 200) {
    Serial.print("HTTP_OK:");
    Serial.println(http.getString());
  } else {
    Serial.print("HTTP_FAIL:code=");
    Serial.println(code);
  }
  http.end();
}

// Phase 2 of docs/testing/JIG_NETWORK_BRIDGE_SPEC.md -- office-WiFi/MQTT
// bridge. Same broker/credentials as firmware/include/Config.h and the
// app's MqttCommander.kt/MqttChecker.kt -- a single value shared across
// the whole fleet, not per-device; update all these places together if
// it ever changes. PubSubClient (not a fancier async client) matches
// exactly what the DUT's own MQTTClient.cpp already uses -- same
// library, same behavior, nothing new to validate.
const char* MQTT_BROKER = "mqtt.agrisenseandcontrol.in";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "fg1-device";
const char* MQTT_PASS = "asacfg1";

WiFiClient mqttNetClient;
PubSubClient mqttClient(mqttNetClient);
String mqttDeviceId;
String mqttLatestStatus;     // "" means none received yet
bool mqttHasStatus = false;

void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  String s;
  s.reserve(len);
  for (unsigned int i = 0; i < len; i++) s += (char)payload[i];
  mqttLatestStatus = s;
  mqttHasStatus = true;
}

// Connects to the shared broker and subscribes to this DUT's status
// topic -- mirrors MqttCommander.kt's connect() exactly, just without
// the phone-side cellular-network-binding dance, since the jig's WiFi
// *is* the office network already (no competing radio/interface to
// route around, unlike the phone).
void handleMqttConnect(const String &deviceId) {
  mqttDeviceId = deviceId;
  mqttLatestStatus = "";
  mqttHasStatus = false;
  // PubSubClient's default MQTT_MAX_PACKET_SIZE is 256 bytes -- any
  // incoming message larger than that is silently dropped, never reaching
  // onMqttMessage() at all (no error, no log, nothing). The DUT's own
  // status JSON is well over that (device_id/firmware/rtc/wifi/mqtt/ap/
  // cycle/liters fields all together), which is exactly why the DUT's own
  // MQTTClient.cpp calls setBufferSize(24576) for the same reason -- this
  // needs far less (status is the only thing subscribed to here, not the
  // DUT's own history-range payloads), but the default is still nowhere
  // near enough. 2026-09-18 bench finding: mqtt_connect() succeeded and
  // the DUT was confirmed actively publishing every 5s, but mqtt_status()
  // returned nothing at all across a clean 20s poll -- this was why.
  mqttClient.setBufferSize(2048);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(onMqttMessage);
  String clientId = "jig-" + deviceId;
  if (!mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.print("MQTT_FAIL:rc=");
    Serial.println(mqttClient.state());
    return;
  }
  String statusTopic = "agrisense/FG1/" + deviceId + "/status";
  mqttClient.subscribe(statusTopic.c_str());
  Serial.println("MQTT_OK");
}

// Publishes <json> to this DUT's command topic -- fire-and-forget, same
// semantics as the broker itself provides (no per-command ack path).
void handleMqttCmd(const String &json) {
  if (mqttDeviceId.length() == 0 || !mqttClient.connected()) {
    Serial.println("MQTT_FAIL:not connected");
    return;
  }
  String cmdTopic = "agrisense/FG1/" + mqttDeviceId + "/command";
  if (mqttClient.publish(cmdTopic.c_str(), json.c_str())) {
    Serial.println("MQTT_SENT");
  } else {
    Serial.println("MQTT_FAIL:publish failed");
  }
}

// Returns the latest status message received since MQTT_CONNECT (and
// clears it -- same "drain and hand back the freshest" contract as
// MqttCommander.latestStatus(), just synchronous here since mqttClient.
// loop() in the main loop() below is what actually receives messages in
// the background between serial commands).
void handleMqttStatus() {
  if (!mqttHasStatus) {
    Serial.println("STATUS_NONE");
    return;
  }
  Serial.print("STATUS:");
  Serial.println(mqttLatestStatus);
  mqttHasStatus = false;
}

// Pulse timing — fast enough for a quick test, slow enough to be a
// clean digital edge for the DUT's flow sensor interrupt.
const int PULSE_HIGH_US = 500;
const int PULSE_LOW_US  = 500;

String inputLine;

void setup() {
  Serial.begin(115200);
  pinMode(PULSE_OUT_PIN, OUTPUT);
  digitalWrite(PULSE_OUT_PIN, LOW);
  // Plain INPUT, not INPUT_PULLUP -- GPIO35 has no internal pull-up
  // (see header comment). Relies on the Tester PCB's own external
  // pull-up: open contact (relay OFF) reads HIGH, closed contact
  // (relay ON, NO shorted to jig GND) reads LOW.
  pinMode(RELAY_SENSE_PIN, INPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  // 2026-09-18: auto-starts a free-running 1Hz square wave at boot, no
  // serial command needed -- this build is deliberately just that and
  // nothing else, so the board can be powered however and left alone
  // (no laptop, no active connection required) while probing D23/D35
  // directly. Everything else in this sketch (PING, HTTP/MQTT bridge,
  // etc.) still works if something does talk to it, but nothing needs
  // to for the square wave itself to keep running.
  handleSquare("1");
}

void emitPulses(int count) {
  digitalWrite(STATUS_LED_PIN, HIGH);
  for (int i = 0; i < count; i++) {
    digitalWrite(PULSE_OUT_PIN, HIGH);
    delayMicroseconds(PULSE_HIGH_US);
    digitalWrite(PULSE_OUT_PIN, LOW);
    delayMicroseconds(PULSE_LOW_US);
  }
  digitalWrite(STATUS_LED_PIN, LOW);
}

// Continuous, non-blocking square wave on PULSE_OUT_PIN -- 2026-09-18
// bench need: PULSE:<n>'s individual pulses are only 500us high, far too
// brief for a multimeter to register (its slow averaging reads a
// ~0.1%-duty-cycle pulse train as indistinguishable from 0V) even though
// the pin is genuinely toggling -- confirmed via the STATUS_LED_PIN,
// driven by this same code path, being visibly blinking. A real
// sustained square wave (equal HIGH/LOW halves at a requested rate) is
// the actual right tool for probing with a meter or scope. Toggled from
// loop() via millis(), not delay()/blocking -- same non-blocking style
// as the rest of this sketch, so serial commands (including SQUARE:STOP)
// keep working while it runs.
bool squareWaveActive = false;
uint32_t squareHalfPeriodMs = 500;
uint32_t squareLastToggleMs = 0;
bool squarePinHigh = false;

void handleSquare(const String &args) {
  if (args == "STOP") {
    squareWaveActive = false;
    digitalWrite(PULSE_OUT_PIN, LOW);
    digitalWrite(STATUS_LED_PIN, LOW);
    Serial.println("OK:SQUARE_STOPPED");
    return;
  }
  float hz = args.toFloat();
  if (hz <= 0 || hz > 1000) {
    Serial.println("ERR:bad rate");
    return;
  }
  squareHalfPeriodMs = (uint32_t)(500.0f / hz);  // half period, ms
  squareWaveActive = true;
  squarePinHigh = true;
  digitalWrite(PULSE_OUT_PIN, HIGH);
  digitalWrite(STATUS_LED_PIN, HIGH);
  squareLastToggleMs = millis();
  Serial.print("OK:SQUARE:");
  Serial.println(hz);
}

// Called every loop() iteration -- toggles the pin exactly on schedule
// regardless of what else loop() is doing that tick (serial commands,
// MQTT servicing), the same non-blocking pattern the rest of this
// sketch already uses for the WiFi/HTTP/MQTT bridge work.
void pollSquareWave() {
  if (!squareWaveActive) return;
  if (millis() - squareLastToggleMs < squareHalfPeriodMs) return;
  squarePinHigh = !squarePinHigh;
  digitalWrite(PULSE_OUT_PIN, squarePinHigh ? HIGH : LOW);
  digitalWrite(STATUS_LED_PIN, squarePinHigh ? HIGH : LOW);
  squareLastToggleMs += squareHalfPeriodMs;
}

void handleCommand(const String &line) {
  if (line == "PING") {
    Serial.println("PONG");
    return;
  }

  if (line == "RELAY?") {
    bool on = digitalRead(RELAY_SENSE_PIN) == LOW;
    Serial.println(on ? "RELAY:ON" : "RELAY:OFF");
    return;
  }

  if (line.startsWith("PULSE:")) {
    int count = line.substring(6).toInt();
    if (count > 0) {
      emitPulses(count);
      Serial.print("OK:");
      Serial.println(count);
    } else {
      Serial.println("ERR:bad count");
    }
    return;
  }

  if (line.startsWith("JOIN_AP:")) {
    handleJoinAp(line.substring(8));
    return;
  }

  if (line == "LEAVE_WIFI") {
    handleLeaveWifi();
    return;
  }

  if (line == "WIFI_STATUS?") {
    handleWifiStatus();
    return;
  }

  if (line.startsWith("HTTP_CMD:")) {
    handleHttpCmd(line.substring(9));
    return;
  }

  if (line.startsWith("MQTT_CONNECT:")) {
    handleMqttConnect(line.substring(13));
    return;
  }

  if (line.startsWith("MQTT_CMD:")) {
    handleMqttCmd(line.substring(9));
    return;
  }

  if (line == "MQTT_STATUS?") {
    handleMqttStatus();
    return;
  }

  if (line.startsWith("SQUARE:")) {
    handleSquare(line.substring(7));
    return;
  }

  Serial.println("ERR:unknown command");
}

void loop() {
  // Services incoming MQTT messages (onMqttMessage callback) and keeps
  // the connection alive in the background -- independent of whatever
  // serial command is or isn't being processed right now, so a status
  // message published between two MQTT_STATUS? polls isn't missed.
  if (mqttClient.connected()) mqttClient.loop();

  pollSquareWave();

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      inputLine.trim();
      if (inputLine.length() > 0) handleCommand(inputLine);
      inputLine = "";
    } else if (c != '\r') {
      inputLine += c;
    }
  }
}
