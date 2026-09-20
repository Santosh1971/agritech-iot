#include <Arduino.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_system.h>

// ---------------------------------------------------------------------
// WPC factory test jig -- runs on the WM1 board.
//
// The jig plays the *other side* of whatever unit is under test (DUT):
//   Master DUT -> jig emulates a Pump Node (PUMPEMU): joins, records every
//                 LEVEL_CMD it receives, answers with a CMD_ACK carrying
//                 settable fake ADC values. Relays RL1..RL4 wired to the
//                 Master's IN1..IN4 float inputs let a host script drive
//                 the water level and check the resulting commands.
//   Pump DUT   -> jig emulates the Master (MASTEREMU): accepts the join,
//                 sends ON/OFF commands, checks the ACK, and senses the
//                 Pump's relay contact on a digital input. PWM outputs
//                 (through an RC filter) feed the Pump's IN1/IN4 analog
//                 inputs.
// It also speaks WiFi so it can join a DUT's SoftAP and poke its HTTP API.
//
// Control is a line-based serial protocol driven by the host script
// (testjig/host/wpc_test.py). Every reply line starts with '@':
//   @OK key=value ...     @ERR reason     @DATA payload     @EVT ...
// The packet format and radio parameters are identical to the nodes; see
// docs/WPC_LoRa_Protocol_v0.3.md.
// ---------------------------------------------------------------------

#define JIG_FW_VERSION "0.1.0"

// ---- WM1 board pin map (from the WM1 schematic) ----------------------
#define PIN_NSS    5
#define PIN_SCK    18
#define PIN_MOSI   23
#define PIN_MISO   19
#define PIN_RESET  25
#define PIN_BUSY   27
#define PIN_DIO1   26
#define PIN_LORA_LED 4

#define PIN_RLY_DATA  17    // 74HC595 chain: relays RL1..RL6 on QA..QF
#define PIN_RLY_CLK   16
#define PIN_RLY_LATCH 13

// Fixture wiring -- change here if the jig is wired differently.
//   SENSE  : reads the Pump DUT's relay dry contact (contact between the
//            pin and GND; closed = LOW). WM1 "IN1" / No Power (NP) input,
//            GPIO14, 10k board pull-up (R7) plus the internal pull-up.
//            (The FL/GPIO36 input was tried first and floated -- no usable
//            pull-up -- so it is not used.)
//   AOUT1/2: PWM -> RC filter -> Pump DUT IN1 / IN4. WM1 "IN2"/"IN3" pins,
//            re-purposed as outputs (no DAC on this board; GPIO25/26 are LoRa).
#define PIN_SENSE  14
#define PIN_AOUT1  32
#define PIN_AOUT2  33
#define AOUT_LEDC_FREQ 20000
#define AOUT_LEDC_BITS 8

// ---- radio parameters: must match the nodes ---------------------------
#define LORA_FREQ_MHZ 866.0
#define LORA_BW_KHZ   125.0
#define LORA_SF       9
#define LORA_CR       7
#define PROTO_VERSION 1

enum MsgType : uint8_t {
  MSG_JOIN_REQUEST = 0x01,
  MSG_JOIN_ACCEPT  = 0x02,
  MSG_LEVEL_CMD    = 0x10,
  MSG_CMD_ACK      = 0x11,
};

SPIClass loraSPI(HSPI);
SX1262 radio = new Module(PIN_NSS, PIN_DIO1, PIN_RESET, PIN_BUSY, loraSPI);
volatile bool operationDone = false;
void ICACHE_RAM_ATTR onRadioAction() { operationDone = true; }

enum Mode { MODE_IDLE, MODE_PUMPEMU, MODE_MASTEREMU };
Mode mode = MODE_IDLE;
bool radioOk = false;
int8_t txPowerDbm = 14;

uint32_t emuMasterId = 0;   // the Master ID we are emulating (MASTEREMU) or joining (PUMPEMU)
uint16_t emuPumpId = 0;

uint8_t txSeq = 0;
uint8_t txPacket[48];

uint32_t badCrc = 0, foreignPkts = 0;

// ---- relays (2x 74HC595, same scheme as the WM1 firmware) -------------
uint8_t relayByte = 0;
uint8_t ledByte = 0;
void pushRelays() {
  digitalWrite(PIN_RLY_LATCH, LOW);
  shiftOut(PIN_RLY_DATA, PIN_RLY_CLK, MSBFIRST, ledByte);    // downstream chip first
  shiftOut(PIN_RLY_DATA, PIN_RLY_CLK, MSBFIRST, relayByte);
  digitalWrite(PIN_RLY_LATCH, HIGH);
}

// ---- helpers ----------------------------------------------------------
uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
  }
  return crc;
}

uint8_t syncWordFor(uint32_t id) {
  return (uint8_t)((id ^ (id >> 8) ^ (id >> 16) ^ (id >> 24)) & 0xFF);
}

void reply(const char* tag, const String& msg) {
  Serial.print('@');
  Serial.print(tag);
  Serial.print(' ');
  Serial.println(msg);
}

String macHex() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char b[13];
  snprintf(b, sizeof(b), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(b);
}

String hex8(uint32_t v) {
  char b[9];
  snprintf(b, sizeof(b), "%08X", (unsigned int)v);
  return String(b);
}

size_t buildPacket(uint8_t type, uint32_t masterId, uint8_t slot, uint8_t seq,
                   const uint8_t* payload, size_t plen) {
  size_t i = 0;
  txPacket[i++] = PROTO_VERSION;
  txPacket[i++] = type;
  txPacket[i++] = masterId >> 24;
  txPacket[i++] = masterId >> 16;
  txPacket[i++] = masterId >> 8;
  txPacket[i++] = masterId;
  txPacket[i++] = slot;
  txPacket[i++] = seq;
  for (size_t p = 0; p < plen; p++) txPacket[i++] = payload[p];
  uint16_t crc = crc16(txPacket, i);
  txPacket[i++] = crc >> 8;
  txPacket[i++] = crc & 0xFF;
  return i;
}

void ledPulse() {
  digitalWrite(PIN_LORA_LED, HIGH);
  delay(15);
  digitalWrite(PIN_LORA_LED, LOW);
}

// ---- radio ------------------------------------------------------------
bool radioStart(uint32_t masterId) {
  int st = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                       syncWordFor(masterId), txPowerDbm, 8, 0, false);
  if (st != RADIOLIB_ERR_NONE) {
    radioOk = false;
    return false;
  }
  radio.setDio1Action(onRadioAction);
  radio.startReceive();
  radioOk = true;
  return true;
}

bool sendPacket(size_t len) {
  operationDone = false;
  if (radio.startTransmit(txPacket, len) != RADIOLIB_ERR_NONE) return false;
  uint32_t t0 = millis();
  while (!operationDone && millis() - t0 < 2500) delay(1);
  bool ok = operationDone;
  operationDone = false;
  radio.startReceive();
  return ok;
}

struct Rx {
  uint8_t buf[64];
  int len;
  float rssi;
  float snr;
};

// Non-blocking: returns true only for a packet that passed CRC, has our
// protocol version and is addressed to the Master ID we're working with.
bool pollRadio(Rx& rx) {
  if (!operationDone) return false;
  operationDone = false;
  int len = radio.getPacketLength();
  bool good = false;
  if (len >= 10 && len <= (int)sizeof(rx.buf)) {
    int st = radio.readData(rx.buf, len);
    if (st == RADIOLIB_ERR_NONE) {
      uint16_t rxCrc = (rx.buf[len - 2] << 8) | rx.buf[len - 1];
      uint32_t mid = ((uint32_t)rx.buf[2] << 24) | ((uint32_t)rx.buf[3] << 16) |
                     ((uint32_t)rx.buf[4] << 8) | rx.buf[5];
      if (crc16(rx.buf, len - 2) != rxCrc) badCrc++;
      else if (rx.buf[0] != PROTO_VERSION || mid != emuMasterId) foreignPkts++;
      else {
        rx.len = len;
        rx.rssi = radio.getRSSI();
        rx.snr = radio.getSNR();
        good = true;
        ledPulse();
      }
    } else if (st == RADIOLIB_ERR_CRC_MISMATCH) {
      badCrc++;
    }
  }
  radio.startReceive();
  return good;
}

// ---- pump emulator (jig acts as a Pump Node; DUT = Master) ------------
bool pumpJoined = false;
uint8_t pumpSlot = 0xFF;
uint32_t pumpJoinLastTry = 0;
uint32_t pumpCmdCount = 0, pumpAckCount = 0;
int pumpLastCmd = -1;
uint8_t pumpLastSeq = 0;
float pumpLastRssi = 0, pumpLastSnr = 0;
uint32_t pumpLastCmdMs = 0;
uint16_t emuAdc1 = 0, emuAdc4 = 0;
bool pumpNoAck = false;

void pumpSendJoin() {
  uint8_t payload[2] = { (uint8_t)(emuPumpId >> 8), (uint8_t)(emuPumpId & 0xFF) };
  size_t n = buildPacket(MSG_JOIN_REQUEST, emuMasterId, 0xFF, 0, payload, 2);
  sendPacket(n);
}

void pumpEmuHandle(const Rx& rx) {
  uint8_t type = rx.buf[1], slot = rx.buf[6], seq = rx.buf[7];
  if (!pumpJoined) {
    if (type == MSG_JOIN_ACCEPT && rx.len >= 13) {
      uint16_t accepted = ((uint16_t)rx.buf[8] << 8) | rx.buf[9];
      if (accepted != emuPumpId) return;
      pumpSlot = rx.buf[10];
      pumpJoined = true;
      pumpLastRssi = rx.rssi;
      pumpLastSnr = rx.snr;
      reply("EVT", String("JOINED slot=") + pumpSlot + " rssi=" + rx.rssi + " snr=" + rx.snr);
    }
    return;
  }
  if (type != MSG_LEVEL_CMD || slot != pumpSlot) return;
  pumpCmdCount++;
  pumpLastCmd = rx.buf[8] ? 1 : 0;
  pumpLastSeq = seq;
  pumpLastRssi = rx.rssi;
  pumpLastSnr = rx.snr;
  pumpLastCmdMs = millis();
  reply("EVT", String("CMD state=") + pumpLastCmd + " seq=" + seq + " rssi=" + rx.rssi + " snr=" + rx.snr);
  if (pumpNoAck) return;
  uint8_t payload[7] = { (uint8_t)pumpLastCmd, 0, 0,
                         (uint8_t)(emuAdc1 >> 8), (uint8_t)(emuAdc1 & 0xFF),
                         (uint8_t)(emuAdc4 >> 8), (uint8_t)(emuAdc4 & 0xFF) };
  size_t n = buildPacket(MSG_CMD_ACK, emuMasterId, pumpSlot, seq, payload, 7);
  if (sendPacket(n)) pumpAckCount++;
}

void pumpEmuLoop() {
  Rx rx;
  if (pollRadio(rx)) pumpEmuHandle(rx);
  if (!pumpJoined && millis() - pumpJoinLastTry > 1200) {
    pumpJoinLastTry = millis();
    pumpSendJoin();
  }
}

// ---- master emulator (jig acts as the Master; DUT = Pump Node) --------
bool masterJoined = false;
uint16_t masterPumpId = 0;
const uint8_t MASTER_SLOT = 0;   // the jig only ever tests one DUT at a time
uint8_t masterSeq = 0;

void masterEmuHandle(const Rx& rx) {
  if (rx.buf[1] != MSG_JOIN_REQUEST || rx.len < 12) return;
  masterPumpId = ((uint16_t)rx.buf[8] << 8) | rx.buf[9];
  masterJoined = true;
  uint8_t payload[3] = { (uint8_t)(masterPumpId >> 8), (uint8_t)(masterPumpId & 0xFF), MASTER_SLOT };
  size_t n = buildPacket(MSG_JOIN_ACCEPT, emuMasterId, MASTER_SLOT, masterSeq++, payload, 3);
  sendPacket(n);
  reply("EVT", String("JOIN pumpId=") + masterPumpId + " rssi=" + rx.rssi + " snr=" + rx.snr);
}

void masterEmuLoop() {
  Rx rx;
  if (pollRadio(rx)) masterEmuHandle(rx);
}

// ---- console ----------------------------------------------------------
String consoleBuf;

void modeStop() {
  mode = MODE_IDLE;
  pumpJoined = false;
  masterJoined = false;
  if (radioOk) radio.standby();
}

void cmdPumpEmu(const String& args) {
  int sp = args.indexOf(' ');
  String sub = sp < 0 ? args : args.substring(0, sp);
  String rest = sp < 0 ? String("") : args.substring(sp + 1);
  sub.toUpperCase();
  rest.trim();
  if (sub == "START") {                       // PUMPEMU START <masterId8hex> <pumpId>
    int s2 = rest.indexOf(' ');
    uint32_t mid = strtoul(rest.c_str(), nullptr, 16);
    int pid = s2 < 0 ? 9999 : rest.substring(s2 + 1).toInt();
    if (mid == 0) { reply("ERR", "usage: PUMPEMU START <masterId hex8> [pumpId]"); return; }
    modeStop();
    emuMasterId = mid;
    emuPumpId = (uint16_t)pid;
    pumpCmdCount = pumpAckCount = 0;
    pumpLastCmd = -1;
    badCrc = foreignPkts = 0;
    if (!radioStart(mid)) { reply("ERR", "radio init failed"); return; }
    mode = MODE_PUMPEMU;
    pumpJoinLastTry = 0;
    reply("OK", String("pumpemu master=") + hex8(mid) + " pumpId=" + emuPumpId);
  } else if (sub == "STOP") {
    modeStop();
    reply("OK", "stopped");
  } else if (sub == "ADC") {                  // PUMPEMU ADC <in1> <in4>
    int s2 = rest.indexOf(' ');
    emuAdc1 = (uint16_t)rest.toInt();
    emuAdc4 = s2 < 0 ? 0 : (uint16_t)rest.substring(s2 + 1).toInt();
    reply("OK", String("adc1=") + emuAdc1 + " adc4=" + emuAdc4);
  } else if (sub == "NOACK") {                // PUMPEMU NOACK <0|1>
    pumpNoAck = rest.toInt() != 0;
    reply("OK", String("noack=") + pumpNoAck);
  } else if (sub == "STATUS") {
    reply("OK", String("mode=pumpemu joined=") + pumpJoined + " slot=" + (pumpJoined ? (int)pumpSlot : -1) +
                " cmds=" + pumpCmdCount + " acks=" + pumpAckCount + " lastCmd=" + pumpLastCmd +
                " lastSeq=" + pumpLastSeq + " lastCmdAgeMs=" + (pumpCmdCount ? (millis() - pumpLastCmdMs) : 0) +
                " rssi=" + pumpLastRssi + " snr=" + pumpLastSnr + " badCrc=" + badCrc + " foreign=" + foreignPkts);
  } else {
    reply("ERR", "PUMPEMU START|STOP|ADC|NOACK|STATUS");
  }
}

void cmdMasterEmu(const String& args) {
  int sp = args.indexOf(' ');
  String sub = sp < 0 ? args : args.substring(0, sp);
  String rest = sp < 0 ? String("") : args.substring(sp + 1);
  sub.toUpperCase();
  rest.trim();
  if (sub == "START") {                       // MASTEREMU START <masterId8hex>
    uint32_t mid = strtoul(rest.c_str(), nullptr, 16);
    if (mid == 0) { reply("ERR", "usage: MASTEREMU START <masterId hex8>"); return; }
    modeStop();
    emuMasterId = mid;
    badCrc = foreignPkts = 0;
    if (!radioStart(mid)) { reply("ERR", "radio init failed"); return; }
    mode = MODE_MASTEREMU;
    reply("OK", String("masteremu master=") + hex8(mid));
  } else if (sub == "STOP") {
    modeStop();
    reply("OK", "stopped");
  } else if (sub == "STATUS") {
    reply("OK", String("mode=masteremu joined=") + masterJoined + " pumpId=" + masterPumpId +
                " badCrc=" + badCrc + " foreign=" + foreignPkts);
  } else if (sub == "CMD") {                  // MASTEREMU CMD <0|1> [attempts]
    if (mode != MODE_MASTEREMU || !masterJoined) { reply("ERR", "not joined"); return; }
    int state = rest.toInt() ? 1 : 0;
    int s2 = rest.indexOf(' ');
    int attempts = s2 < 0 ? 3 : rest.substring(s2 + 1).toInt();
    if (attempts < 1) attempts = 1;
    for (int a = 0; a < attempts; a++) {
      uint8_t payload[1] = { (uint8_t)state };
      uint8_t seq = masterSeq++;
      size_t n = buildPacket(MSG_LEVEL_CMD, emuMasterId, MASTER_SLOT, seq, payload, 1);
      if (!sendPacket(n)) { reply("ERR", "tx failed"); return; }
      uint32_t t0 = millis();
      Rx rx;
      while (millis() - t0 < 2500) {
        if (pollRadio(rx)) {
          if (rx.buf[1] == MSG_CMD_ACK && rx.buf[6] == MASTER_SLOT && rx.len >= 17) {
            uint16_t a1 = ((uint16_t)rx.buf[11] << 8) | rx.buf[12];
            uint16_t a4 = ((uint16_t)rx.buf[13] << 8) | rx.buf[14];
            reply("OK", String("ack=1 seq=") + rx.buf[7] + " relay=" + rx.buf[8] + " in1dig=" + rx.buf[9] +
                        " in4dig=" + rx.buf[10] + " adc1=" + a1 + " adc4=" + a4 +
                        " rssi=" + rx.rssi + " snr=" + rx.snr + " attempt=" + (a + 1));
            return;
          }
          masterEmuHandle(rx);   // a re-join during the wait must still be answered
        }
        delay(1);
      }
    }
    reply("ERR", "noack");
  } else {
    reply("ERR", "MASTEREMU START|STOP|STATUS|CMD");
  }
}

bool wifiWait(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(100);
  }
  return false;
}

void handleConsoleLine(String line) {
  line.trim();
  if (!line.length()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String args = sp < 0 ? String("") : line.substring(sp + 1);
  args.trim();
  cmd.toUpperCase();

  if (cmd == "ID") {
    reply("OK", String("board=WPC-JIG fw=") + JIG_FW_VERSION + " mac=" + macHex());
  } else if (cmd == "RELAY") {                // RELAY <1-6> <0|1>
    int n = args.toInt();
    int v = args.substring(args.indexOf(' ') + 1).toInt();
    if (args.indexOf(' ') < 0 || n < 1 || n > 6) { reply("ERR", "usage: RELAY <1-6> <0|1>"); return; }
    if (v) relayByte |= (1 << (n - 1)); else relayByte &= ~(1 << (n - 1));
    pushRelays();
    reply("OK", String("relays=") + relayByte);
  } else if (cmd == "RELAYS") {               // RELAYS <mask 0-63>
    relayByte = (uint8_t)(args.toInt() & 0x3F);
    pushRelays();
    reply("OK", String("relays=") + relayByte);
  } else if (cmd == "SENSE") {
    reply("OK", String("contact=") + (digitalRead(PIN_SENSE) == LOW ? 1 : 0));
  } else if (cmd == "AOUT") {                 // AOUT <1|2> <duty 0-255>
    int ch = args.toInt();
    int duty = args.substring(args.indexOf(' ') + 1).toInt();
    if (args.indexOf(' ') < 0 || (ch != 1 && ch != 2) || duty < 0 || duty > 255) {
      reply("ERR", "usage: AOUT <1|2> <duty 0-255>");
      return;
    }
    ledcWrite(ch - 1, duty);
    reply("OK", String("aout") + ch + "=" + duty);
  } else if (cmd == "TXPOWER") {              // TXPOWER <dBm> -- applies at the next PUMPEMU/MASTEREMU START
    int p = args.toInt();
    if (p < -9 || p > 22) { reply("ERR", "usage: TXPOWER <-9..22>"); return; }
    txPowerDbm = (int8_t)p;
    reply("OK", String("txPower=") + txPowerDbm);
  } else if (cmd == "PUMPEMU") {
    cmdPumpEmu(args);
  } else if (cmd == "MASTEREMU") {
    cmdMasterEmu(args);
  } else if (cmd == "WIFISCAN") {             // WIFISCAN <ssid>
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    int found = 0, rssi = 0;
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == args) { found = 1; rssi = WiFi.RSSI(i); break; }
    }
    WiFi.scanDelete();
    reply("OK", String("found=") + found + " rssi=" + rssi + " networks=" + n);
  } else if (cmd == "WIFICONNECT") {          // WIFICONNECT <ssid> [password]
    int s2 = args.indexOf(' ');
    String ssid = s2 < 0 ? args : args.substring(0, s2);
    String pass = s2 < 0 ? String("") : args.substring(s2 + 1);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
    if (wifiWait(15000)) reply("OK", String("ip=") + WiFi.localIP().toString() + " gw=" + WiFi.gatewayIP().toString());
    else reply("ERR", "wifi connect timeout");
  } else if (cmd == "WIFIDISCONNECT") {
    WiFi.disconnect(false, false);
    reply("OK", "disconnected");
  } else if (cmd == "HTTPGET" || cmd == "HTTPPOST") {   // HTTPGET <path>   |   HTTPPOST <path> <json>
    if (WiFi.status() != WL_CONNECTED) { reply("ERR", "wifi not connected"); return; }
    int s2 = args.indexOf(' ');
    String path = s2 < 0 ? args : args.substring(0, s2);
    String body = s2 < 0 ? String("") : args.substring(s2 + 1);
    HTTPClient http;
    http.setTimeout(6000);
    http.begin("http://" + WiFi.gatewayIP().toString() + path);
    int code;
    if (cmd == "HTTPPOST") {
      http.addHeader("Content-Type", "application/json");
      code = http.POST(body);
    } else {
      code = http.GET();
    }
    String resp = code > 0 ? http.getString() : String("");
    http.end();
    if (code <= 0) { reply("ERR", String("http error ") + code); return; }
    size_t fullLen = resp.length();
    if (resp.length() > 3000) resp = resp.substring(0, 3000);
    resp.replace("\n", " ");
    reply("DATA", resp);   // DATA lines always precede the terminating @OK/@ERR
    reply("OK", String("status=") + code + " len=" + fullLen);
  } else if (cmd == "RADIO") {
    reply("OK", String("radioOk=") + radioOk + " mode=" + (mode == MODE_PUMPEMU ? "pumpemu" : mode == MODE_MASTEREMU ? "masteremu" : "idle"));
  } else if (cmd == "RESET") {
    modeStop();
    relayByte = 0;
    pushRelays();
    ledcWrite(0, 0);
    ledcWrite(1, 0);
    reply("OK", "jig idle, relays off, analog outputs 0");
  } else if (cmd == "REBOOT") {
    reply("OK", "rebooting");
    delay(200);
    ESP.restart();
  } else {
    reply("ERR", "unknown command (ID RELAY RELAYS SENSE AOUT TXPOWER PUMPEMU MASTEREMU WIFISCAN WIFICONNECT WIFIDISCONNECT HTTPGET HTTPPOST RADIO RESET REBOOT)");
  }
}

void pollConsole() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      String l = consoleBuf;
      consoleBuf = "";
      handleConsoleLine(l);
    } else if (consoleBuf.length() < 240) {
      consoleBuf += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_RLY_DATA, OUTPUT);
  pinMode(PIN_RLY_CLK, OUTPUT);
  pinMode(PIN_RLY_LATCH, OUTPUT);
  pinMode(PIN_LORA_LED, OUTPUT);
  pinMode(PIN_SENSE, INPUT_PULLUP);
  relayByte = 0;
  ledByte = 0;
  pushRelays();

  ledcSetup(0, AOUT_LEDC_FREQ, AOUT_LEDC_BITS);
  ledcSetup(1, AOUT_LEDC_FREQ, AOUT_LEDC_BITS);
  ledcAttachPin(PIN_AOUT1, 0);
  ledcAttachPin(PIN_AOUT2, 1);
  ledcWrite(0, 0);
  ledcWrite(1, 0);

  loraSPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);

  reply("OK", String("jig ready fw=") + JIG_FW_VERSION);
}

void loop() {
  pollConsole();
  if (mode == MODE_PUMPEMU) pumpEmuLoop();
  else if (mode == MODE_MASTEREMU) masterEmuLoop();
  delay(1);
}
