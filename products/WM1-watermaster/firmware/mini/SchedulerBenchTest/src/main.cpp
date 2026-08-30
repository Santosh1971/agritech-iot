// Water Manager-Mini — Scheduler Bench Test
//
// Runs the real Scheduler/IrrigationController/RelayController stack
// against the confirmed-working RTC + relay chain. Two canned test
// Programs are defined below purely for bench testing — real
// programs will eventually come from the app/backend, not hardcoded
// here. Flow/pressure/IN1 are NOT required for any of this: every
// test program below is time-based, and IN1 pause/resume is exercised
// via serial commands ('p'/'o') instead of the real "No Power" line,
// since that circuit isn't being tested yet.
//
// Serial menu:
//   s - show current scheduler/relay state
//   1 - trigger Program A now (single valve, no dosing, 30s)
//   2 - trigger Program B now (two valves + mid-dosing, 60s run, 10s dose)
//   p - simulate power loss (IN1 LOW) -> pause
//   o - simulate power restored (IN1 HIGH) -> resume
//   x - force-stop everything (all relays off)

#include <Arduino.h>
#include <Wire.h>
#include "RelayController.h"
#include "IrrigationController.h"
#include "Scheduler.h"

#define RLY_DATA  17
#define RLY_CLK   16
#define RLY_LATCH 13
#define DS1307_ADDR 0x68

ShiftRegisterRelayController relays(RLY_DATA, RLY_CLK, RLY_LATCH);
IrrigationController irrigation(relays);
Scheduler scheduler(irrigation);

Program programA;
Program programB;
Program customProgram;

// --- DS1307 full date/time read, BCD decode ---------------------------
uint8_t bcd2dec(uint8_t v) { return ((v / 16) * 10) + (v % 16); }

bool ds1307ReadFull(struct tm& out) {
  Wire.beginTransmission(DS1307_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  Wire.requestFrom(DS1307_ADDR, 7);
  if (Wire.available() < 7) return false;
  out.tm_sec  = bcd2dec(Wire.read() & 0x7F);
  out.tm_min  = bcd2dec(Wire.read());
  out.tm_hour = bcd2dec(Wire.read() & 0x3F);
  Wire.read();  // day-of-week, unused (we compute our own)
  out.tm_mday = bcd2dec(Wire.read());
  out.tm_mon  = bcd2dec(Wire.read()) - 1;   // struct tm months are 0-11
  out.tm_year = bcd2dec(Wire.read()) + 100; // DS1307 gives 00-99 -> 2000s; struct tm wants years-since-1900
  out.tm_isdst = 0;
  return true;
}

time_t rtcBaseEpoch = 0;
uint32_t rtcBaseMillis = 0;

bool syncClockFromRtc() {
  struct tm t;
  if (!ds1307ReadFull(t)) return false;
  rtcBaseEpoch = mktime(&t);
  rtcBaseMillis = millis();
  Serial.printf("[Clock] Synced from RTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  return true;
}

time_t currentTime() {
  return rtcBaseEpoch + (millis() - rtcBaseMillis) / 1000;
}

// --- DS1307 set from this sketch's compile time (standard bring-up pattern) ---
uint8_t dec2bcd(uint8_t v) { return ((v / 10) * 16) + (v % 10); }

int monthFromStr(const char* mmm) {
  const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* p = strstr(months, mmm);
  return p ? (p - months) / 3 : 0;
}

void ds1307SetFromCompileTime() {
  // __DATE__ = "Mmm dd yyyy", __TIME__ = "hh:mm:ss"
  char mmm[4]; int day, year, hour, min, sec;
  sscanf(__DATE__, "%3s %d %d", mmm, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);
  int month = monthFromStr(mmm) + 1;

  Wire.beginTransmission(DS1307_ADDR);
  Wire.write(0x00);
  Wire.write(dec2bcd(sec));
  Wire.write(dec2bcd(min));
  Wire.write(dec2bcd(hour));
  Wire.write(1);                       // day-of-week, unused by our code
  Wire.write(dec2bcd(day));
  Wire.write(dec2bcd(month));
  Wire.write(dec2bcd(year - 2000));
  Wire.endTransmission();

  Serial.printf("[Clock] RTC set to build time: %04d-%02d-%02d %02d:%02d:%02d\n",
                year, month, day, hour, min, sec);
}


void defineTestPrograms() {
  // Program A: one valve, no dosing, short 30s run — quick sanity check.
  strcpy(programA.name, "Test A - Valve1");
  programA.sequenceCount = 1;
  programA.repeatMode = RepeatMode::INTERVAL_DAYS;
  programA.intervalDays = 1;
  programA.startHour = 6; programA.startMinute = 0;  // only matters for real scheduled runs
  programA.enabled = true;
  programA.autoStart = false;  // bench-test mode: trigger manually via 's1', not on the clock
  Sequence& sa = programA.sequences[0];
  sa.id = 1; strcpy(sa.name, "Seq A1");
  sa.valveMask = 0b0001;       // valve 0 only (RL3)
  sa.doseEnabled = false;
  sa.runMode = RunMode::TIME_BASED;
  sa.runTargetSec = 30;

  // Program B: two valves together + mid-run dosing — exercises the
  // pump-follows-multiple-valves rule and the dosing-timing logic.
  strcpy(programB.name, "Test B - Valve1+2 + Dose");
  programB.sequenceCount = 1;
  programB.repeatMode = RepeatMode::INTERVAL_DAYS;
  programB.intervalDays = 1;
  programB.startHour = 6; programB.startMinute = 30;
  programB.enabled = true;
  programB.autoStart = false;
  Sequence& sb = programB.sequences[0];
  sb.id = 1; strcpy(sb.name, "Seq B1");
  sb.valveMask = 0b0011;       // valves 0+1 together (RL3+RL4)
  sb.doseEnabled = true;
  sb.doseTiming = DoseTiming::MID;
  sb.doseDurationSec = 10;
  sb.runMode = RunMode::TIME_BASED;
  sb.runTargetSec = 60;

  scheduler.addProgram(&programA);
  scheduler.addProgram(&programB);
}

// Free-form program builder for bench testing arbitrary valve/dosing
// combinations without recompiling — not part of the production
// firmware, just a faster way to exercise the Scheduler/IrrigationController
// than editing defineTestPrograms() and reflashing for every combo.
void defineAndTriggerCustomProgram(const String& line) {
  char maskStr[8] = {0};
  char doseChar = 'n';
  int durSec = 0, doseDur = 0;
  int parsed = sscanf(line.c_str(), "%7s %d %c %d", maskStr, &durSec, &doseChar, &doseDur);
  if (parsed < 2) {
    Serial.println("[Custom] Parse error - need at least '<mask> <durationSec>'");
    return;
  }

  uint8_t mask = 0;
  for (uint8_t i = 0; i < 4 && maskStr[i]; i++) {
    if (maskStr[i] == '1') mask |= (1 << i);
  }
  if (mask == 0) {
    Serial.println("[Custom] Mask is all zeros - no valve would open, refusing to trigger.");
    return;
  }

  strcpy(customProgram.name, "Custom");
  customProgram.sequenceCount = 1;
  customProgram.repeatMode = RepeatMode::INTERVAL_DAYS;
  customProgram.intervalDays = 1;
  customProgram.enabled = true;
  customProgram.autoStart = false;

  Sequence& sc = customProgram.sequences[0];
  sc.id = 1;
  strcpy(sc.name, "CustomSeq");
  sc.valveMask = mask;
  sc.runMode = RunMode::TIME_BASED;
  sc.runTargetSec = (durSec > 0) ? (uint32_t)durSec : 30;
  sc.doseEnabled = (doseChar == 's' || doseChar == 'm' || doseChar == 'e');
  sc.doseTiming = (doseChar == 's') ? DoseTiming::START
                : (doseChar == 'e') ? DoseTiming::END
                : DoseTiming::MID;
  sc.doseDurationSec = (doseDur > 0) ? (uint16_t)doseDur : 10;

  Serial.printf("[Custom] valveMask=0x%02X runTargetSec=%u dose=%s timing=%c dur=%us\n",
                mask, sc.runTargetSec, sc.doseEnabled ? "on" : "off", doseChar, sc.doseDurationSec);
  scheduler.triggerNow(&customProgram, 0);
}

void printStatus() {
  const char* stateNames[] = {"IDLE", "RUNNING", "PAUSED", "QUEUED_WAITING"};
  Serial.printf("State: %s\n", stateNames[(int)scheduler.state()]);
  Serial.printf("Pump: %s  Dosing: %s\n",
                irrigation.getPump() ? "ON" : "off",
                irrigation.getDosing() ? "ON" : "off");
  Serial.print("Valves: ");
  for (uint8_t i = 0; i < IrrigationController::VALVE_COUNT; i++) {
    Serial.printf("V%u=%s ", i + 1, irrigation.getValve(i) ? "ON" : "off");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(21, 22);

  irrigation.begin();       // relays.begin() + allOff()
  relays.allLedsOff();      // explicit: all indicator LEDs off at boot
  scheduler.begin();

  if (!syncClockFromRtc()) {
    Serial.println("WARNING: RTC read failed at boot — scheduler timing will be wrong until synced.");
  }

  defineTestPrograms();

  Serial.println();
  Serial.println("--- Scheduler Bench Test ---");
  Serial.println("s - show status");
  Serial.println("1 - trigger Program A now (Valve1, 30s, no dose)");
  Serial.println("2 - trigger Program B now (Valve1+2, 60s, mid-dose 10s)");
  Serial.println("c - define + trigger a custom program: '<mask4> <durSec> [n/s/m/e] [doseDurSec]'");
  Serial.println("    e.g. '1010 45 m 10' = valves 1+3, 45s run, mid-dose 10s");
  Serial.println("p - simulate power LOSS (pause)");
  Serial.println("o - simulate power RESTORED (resume)");
  Serial.println("x - force-stop everything");
  Serial.println("T - set RTC to this sketch's compile time (do this once if RTC date looks wrong)");
  Serial.println("----------------------------");
}

void loop() {
  scheduler.update(currentTime());

  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 's': printStatus(); break;
      case '1': scheduler.triggerNow(&programA, 0); break;
      case '2': scheduler.triggerNow(&programB, 0); break;
      case 'c': {
        Serial.println("Enter: <valveMask4> <durationSec> [n/s/m/e] [doseDurationSec]");
        while (!Serial.available()) delay(10);
        String line = Serial.readStringUntil('\n');
        line.trim();
        defineAndTriggerCustomProgram(line);
        break;
      }
      case 'p': scheduler.onPowerStateChange(false); break;
      case 'o': scheduler.onPowerStateChange(true); break;
      case 'x': scheduler.forceStop(); Serial.println("Forced stop (Scheduler + relays)."); break;
      case 'T': ds1307SetFromCompileTime(); syncClockFromRtc(); break;
      default: break;
    }
  }
  delay(50);
}
