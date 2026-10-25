#pragma once
// Board drivers for the WM2-WaterMaster: RTC, latching valves, flow and
// pressure inputs, battery monitor, LEDs. Pin map: ../common/wm2_pins.h
#include <Arduino.h>

namespace hw {

// ---- DS1307 RTC, holds UTC ----
bool rtcBegin();
bool rtcRead(uint32_t& utc);      // false if chip missing or clock halted/unset
bool rtcWrite(uint32_t utc);

// ---- Latching valves (DRV8871). Pulses are queued and spaced by cfg.gapS
//      so the driver's capacitors recharge between pulses. ----
void valvesBegin();               // drivers safe, restore last state from NVS
void valvesLoop();                // runs queued pulses
void valveRequest(int v, bool open, bool force = false);  // queue a pulse if state differs
bool valveState(int v);           // last state actually pulsed (or restored)
bool valvePending(int v);         // a pulse is waiting (e.g. no 12V yet)
void valveResync();               // re-pulse both valves to their stored state
void valvePulseRaw(int v, bool open);  // immediate, for valve_test

// ---- Flow inputs ----
void flowBegin();
// Fill per-input L/min (5 s average) and litres since the last call.
void flowSample(float lpm[4], float litresDelta[4]);
float flowTotal(int i);           // litres since boot
uint32_t flowPulses(int i);

// ---- Pressure switch (debounced by cfg.psSettleS) ----
void psBegin();
bool psState();                   // true = pressure (water in pipe)
bool psRaw();

// ---- Battery ----
float battVolts();                // 0 when below the ADC floor (no 12V)
float battRead();                 // fresh averaged reading, updates trend
int battSoc();                    // % from resting-voltage table
const char* battTrend();          // charging / discharging / steady
const char* battLevel();          // ok / warning / save / critical / none
float battDaysLeft();

// ---- LEDs ----
enum WifiLed { WL_OFF, WL_AP, WL_WIFI, WL_MQTT };
void ledsBegin();
void ledsLoop(WifiLed w, bool water, bool bypass);

}  // namespace hw
