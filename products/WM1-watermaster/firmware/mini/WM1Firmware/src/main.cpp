// Water Manager-Mini — Full Firmware (v0.3)
//
// Wires the bench-tested RelayController / IrrigationController /
// Scheduler stack (real relays, real DS1307 RTC, clean pause/resume,
// dosing auto-off fix, reboot-survival checkpointing) to WiFi/SoftAP/
// MQTT/local WebSocket control, mirroring FG1's actual hybrid-transport
// app pattern (see products/FG1-flowguard/mobile-app/flutter_app).
//
// v0.3 adds: fast-blink status LED (mirrors WPC), non-blocking WiFi
// scan (ported from FG1), RTC sync from the app, factory reset, and
// custom relay display names.
//
// A raw-JSON command line typed over USB serial is also accepted — lets
// you issue any command over the bench USB link without first having to
// join the device's own SoftAP.

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "Config.h"
#include "DeviceIdentity.h"
#include "RelayController.h"
#include "IrrigationController.h"
#include "Scheduler.h"
#include "ProgramStore.h"
#include "WiFiManager.h"
#include "Ds1307Clock.h"
#include "RelayNames.h"
#include "SequenceLibrary.h"
#include "Sensors.h"
#include "RunHistory.h"
#include "WiFiScanner.h"
#include "StatusLed.h"
#include "CommandHandler.h"
#include "MqttClientWrapper.h"
#include "LocalServer.h"

#define RLY_DATA  17
#define RLY_CLK   16
#define RLY_LATCH 13

ShiftRegisterRelayController relays(RLY_DATA, RLY_CLK, RLY_LATCH);
IrrigationController irrigation(relays);
Sensors sensors;
RunHistory runHistory;
Scheduler scheduler(irrigation, sensors, runHistory);
ProgramStore programStore;
WiFiManager wifiManager;
MqttClientWrapper mqtt;
LocalServer localServer;
Ds1307Clock rtcClock;
RelayNames relayNames;
SequenceLibrary sequenceLibrary;
WiFiScanner wifiScanner;
StatusLed statusLed;

// Kept as a free function per this firmware's existing ISR convention
// (see the earlier bench sketches) rather than inside Sensors.h, which
// avoids any static-instance-pointer machinery in a header.
void IRAM_ATTR onFlowPulse() { sensors.pulseCount++; }

Program programPool[ProgramStore::MAX_SLOTS];
Program* programSlots[ProgramStore::MAX_SLOTS];
uint8_t programCount = 0;

CommandHandler* commandHandler = nullptr;

bool wifiScanInProgress = false;
bool factoryResetPending = false;
uint32_t factoryResetAt = 0;

uint32_t lastStatusPublish = 0;
uint32_t lastDiagPrint = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(21, 22);

  Serial.printf("\n=== Water Manager-Mini — %s ===\n", computeDeviceId().c_str());

  // Self-test: all 7 status LEDs (Flow/PR1/PR2/IN1/IN2/IN3/LOBATT) plus
  // the onboard WiFi status LED ON for 3s then OFF — same idea as WPC's
  // boot self-test, adapted to this board's LED set. Relays are
  // deliberately NOT included (unlike the LEDs, energizing a pump/valve
  // during a self-test isn't something you want happening automatically
  // at every boot). Direct pin control here, ahead of each component's
  // normal begin() below, which will leave everything in its real
  // starting state once this finishes.
  relays.begin();
  pinMode(StatusLed::PIN, OUTPUT);
  Serial.println("[SelfTest] LEDs ON");
  for (uint8_t bit = 0; bit <= ShiftRegisterRelayController::LED_LOBATT; bit++) relays.setLed(bit, true);
  digitalWrite(StatusLed::PIN, HIGH);
  delay(3000);
  relays.allLedsOff();
  digitalWrite(StatusLed::PIN, LOW);
  Serial.println("[SelfTest] LEDs OFF");

  irrigation.begin();
  scheduler.begin();
  rtcClock.begin();
  relayNames.begin();
  sequenceLibrary.begin();
  sensors.begin();
  attachInterrupt(digitalPinToInterrupt(Sensors::PIN_FLOW), onFlowPulse, RISING);
  runHistory.begin();
  statusLed.begin();
  if (!rtcClock.isRunning()) {
    Serial.println("[RTC] WARNING: oscillator not running — RTC was never set. "
                    "Schedules will not trigger until synced from the app (Local Setup > Sync Time From Phone).");
  } else {
    Serial.printf("[RTC] %s %s\n", rtcClock.dateString().c_str(), rtcClock.timeString().c_str());
  }

  programStore.begin();
  for (uint8_t i = 0; i < ProgramStore::MAX_SLOTS; i++) programSlots[i] = &programPool[i];
  programCount = programStore.loadAll(programSlots);
  for (uint8_t i = 0; i < programCount; i++) scheduler.addProgram(programSlots[i]);
  Serial.printf("[Main] Loaded %u saved program(s)\n", programCount);

  // Reboot-survival: if a program was actively running when power was
  // last lost, this resumes it from its last checkpoint (elapsed run
  // time preserved, not restarted) — same mechanism as an IN1 pause,
  // just triggered by a full reset instead. Must run AFTER programs
  // are loaded above, since it needs to look one up by id.
  scheduler.restoreFromNVS(programSlots, programCount);

  wifiManager.begin();

  static CommandHandler handler(scheduler, irrigation, wifiManager, programStore,
                                 programSlots, programCount, rtcClock, relayNames, sequenceLibrary, sensors, runHistory);
  commandHandler = &handler;

  mqtt.begin(commandHandler);
  localServer.begin(commandHandler);  // called AFTER wifiManager.begin() — see LocalServer.h note

  Serial.println("[Main] Setup complete");
  Serial.println("[Main] Serial commands: 's' = status, or paste a raw JSON command line, e.g.:");
  Serial.println("       {\"cmd\":\"wifi_config\",\"ssid\":\"...\",\"password\":\"...\"}");
}

void loop() {
  wifiManager.loop();
  mqtt.loop(wifiManager.isStaConnected());
  localServer.loop();
  sensors.update();
  // Only ever affects scheduling when this installation has actually
  // opted in (Settings > Hardware Configuration > Water Level Sensor,
  // sent down as set_water_level_enabled) — see Sensors.h's doc comment
  // on why an unconditional read would be unsafe for a borewell farmer
  // with nothing wired to IN2/IN3 at all.
  if (sensors.waterLevelEnabled()) {
    scheduler.onWaterLevelChange(sensors.waterLevelOk());
  }
  scheduler.update(rtcClock.now());
  // LED reflects "a phone joined the WiFi", not "the app's WebSocket is
  // open" — this is the WPC precedent this was meant to mirror in the
  // first place, and it lets the LED confirm the WiFi side is working
  // even before/without the app ever connecting, which is what it's
  // actually for as a debugging aid.
  statusLed.update(wifiManager.apOk(), WiFi.softAPgetStationNum() > 0);

  // Heartbeat, every 2s regardless of activity — the single thing to
  // watch on the serial monitor to know what the LED is actually doing
  // and why, instead of guessing from the outside. AP client count is
  // WiFi.softAPgetStationNum() (anyone joined the AP's WiFi at all);
  // WS client count is localServer.clientCount() (the app's socket is
  // actually open) — these can legitimately differ, and the gap between
  // them is exactly the "phone joined the WiFi but the app never
  // connected" failure mode.
  if (millis() - lastDiagPrint > 2000) {
    lastDiagPrint = millis();
    Serial.printf("[Diag] apOk=%d apClients=%d wsClients=%u staConnected=%d forcedLocal=%d\n",
                  wifiManager.apOk(), WiFi.softAPgetStationNum(),
                  (unsigned)localServer.clientCount(), wifiManager.isStaConnected(),
                  wifiManager.isForcedLocal());
  }

  if (commandHandler->consumeWifiScanRequested()) {
    wifiScanner.startScan();
    wifiScanInProgress = true;
  }
  if (wifiScanInProgress && wifiScanner.checkComplete()) {
    localServer.broadcastTyped("wifi_scan_result", wifiScanner.resultAsJson());
    wifiScanInProgress = false;
  }

  if (commandHandler->consumeFactoryResetRequested()) {
    // Deferred so the command's ack has time to actually reach the app
    // before the socket/AP goes down for the reboot.
    factoryResetPending = true;
    factoryResetAt = millis() + 500;
  }
  if (factoryResetPending && millis() >= factoryResetAt) {
    Serial.println("[Main] Factory reset — clearing saved settings and rebooting");
    Preferences p;
    p.begin(NVS_NAMESPACE, false); p.clear(); p.end();
    p.begin("wm1_rt", false); p.clear(); p.end();
    ESP.restart();
  }

  uint32_t now = millis();
  // Immediate push right after any command (manual_set, trigger_program,
  // force_stop, ...) so the app's UI reflects a relay change as soon as
  // it happens, not up to STATUS_PUBLISH_INTERVAL_MS late — the relay
  // itself clicks instantly, the app shouldn't visibly lag behind it.
  // Resetting lastStatusPublish here avoids also firing the periodic
  // push a moment later for the same state.
  bool duePeriodic = (now - lastStatusPublish > STATUS_PUBLISH_INTERVAL_MS);
  bool changedNow = commandHandler->consumeStatusChanged();  // always evaluated, always consumes the flag
  if (duePeriodic || changedNow) {
    lastStatusPublish = now;
    String status = commandHandler->buildStatusJson();
    mqtt.publishStatus(status);
    localServer.broadcastTyped("status", status);
  }

  if (commandHandler->consumeProgramsChanged()) {
    String progs = commandHandler->buildProgramsJson();
    mqtt.publishPrograms(progs);
    localServer.broadcastTyped("programs", progs);
  }

  if (commandHandler->consumeLibraryChanged()) {
    String lib = commandHandler->buildLibraryJson();
    mqtt.publishLibrary(lib);
    localServer.broadcastTyped("library", lib);
  }

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "s") {
      Serial.println(commandHandler->buildStatusJson());
    } else if (line.length()) {
      commandHandler->handle(line, [](const String& reply) {
        Serial.println(reply);
      });
    }
  }

  // Without this, loop() never yields — fine for the pure-relay bench
  // sketch, but with AsyncTCP/WiFi now also running background tasks on
  // the same core, an unyielding loop() starves the RTOS idle task and
  // trips the task watchdog, resetting the board continuously. Confirmed
  // on real hardware: commands still occasionally got through between
  // resets, which is what made this reboot loop non-obvious at first.
  delay(10);
}
