// Native tests for the irrigation logic: pio test -e native
#include <unity.h>

#include "irrigation_core.h"

using namespace wm2core;

// 2026-10-26 is a Monday. Local epoch for 00:00 that day.
static const uint32_t MON = 1792972800u;  // 2026-10-26 00:00 (wall clock, as UTC)

struct Sim {
  Core c;
  Inputs in;
  uint32_t t;
  int lastEvents = 0;
  Sim(uint32_t start) : t(start) {
    in.timeValid = true;
    in.dtMs = 1000;
  }
  // Advance n seconds, 1 s ticks. `lpmEach` = flow on every input of open valves.
  void run(uint32_t seconds, bool ps, float lpmOpen = 10.0f) {
    for (uint32_t i = 0; i < seconds; i++) {
      t++;
      in.localEpoch = t;
      in.ps = ps;
      for (int k = 0; k < NUM_FLOW; k++) {
        int v = c.cfg.flowValve[k];
        bool flow = v >= 0 && c.valve(v).open && (ps || in.bypass);
        in.lpm[k] = flow ? lpmOpen : 0;
        in.litresDelta[k] = flow ? lpmOpen / 60.0f : 0;
      }
      lastEvents = c.tick(in);
      for (int e = 0; e < lastEvents; e++) log[logN < 64 ? logN++ : 63] = c.events()[e];
    }
  }
  Event log[64];
  int logN = 0;
  int count(EventType type, int valve = -2) {
    int n = 0;
    for (int i = 0; i < logN; i++)
      if (log[i].type == type && (valve == -2 || log[i].valve == valve)) n++;
    return n;
  }
  const Event* find(EventType type, int valve = -2) {
    for (int i = 0; i < logN; i++)
      if (log[i].type == type && (valve == -2 || log[i].valve == valve)) return &log[i];
    return nullptr;
  }
};

static Program timerProg(uint8_t days, uint16_t minutes) {
  Program p;
  p.enabled = true;
  p.type = PROG_TIMER;
  p.daysMask = days;
  p.minutes = minutes;
  return p;
}

void test_weekday() {
  TEST_ASSERT_EQUAL(0, Core::weekday(Core::localDay(MON, 0)));             // Monday
  TEST_ASSERT_EQUAL(6, Core::weekday(Core::localDay(MON - 3600, 0)));      // Sunday night
}

void test_waits_for_water_then_runs_and_finishes() {
  Sim s(MON + 8 * 3600);
  s.c.setProgram(0, timerProg(0x01, 2));  // Monday, 2 minutes
  s.c.cfg.v[1].mode = MODE_OFF;
  s.run(30, false);
  TEST_ASSERT_FALSE(s.c.valve(0).open);
  TEST_ASSERT_TRUE(s.c.valve(0).waiting);
  s.run(60, true);                        // water arrives: opens, runs 60 s
  TEST_ASSERT_TRUE(s.c.valve(0).open);
  s.run(600, false);                      // power cut: keep open, time does not count
  TEST_ASSERT_TRUE(s.c.valve(0).open);
  TEST_ASSERT_UINT32_WITHIN(2, 60, s.c.valve(0).runSec);
  s.run(61, true);                        // power back: finishes the other 60 s
  TEST_ASSERT_FALSE(s.c.valve(0).open);
  TEST_ASSERT_TRUE(s.c.valve(0).done);
  const Event* close = nullptr;
  for (int i = 0; i < s.logN; i++)
    if (s.log[i].type == EV_VALVE && s.log[i].a == 0) close = &s.log[i];
  TEST_ASSERT_NOT_NULL(close);
  TEST_ASSERT_EQUAL(R_PROGRAM_DONE, close->reason);
}

void test_not_programmed_day_stays_closed() {
  Sim s(MON + 8 * 3600);
  s.c.setProgram(0, timerProg(0x02, 10));  // Tuesday only
  s.run(120, true);
  TEST_ASSERT_FALSE(s.c.valve(0).open);
  TEST_ASSERT_FALSE(s.c.valve(0).due);
}

void test_sequential_v1_then_v2() {
  Sim s(MON + 8 * 3600);
  s.c.setProgram(0, timerProg(0x01, 1));
  s.c.setProgram(1, timerProg(0x01, 1));
  s.run(5, true);
  TEST_ASSERT_TRUE(s.c.valve(0).open);
  TEST_ASSERT_FALSE(s.c.valve(1).open);
  s.run(60, true);
  TEST_ASSERT_FALSE(s.c.valve(0).open);
  TEST_ASSERT_TRUE(s.c.valve(1).open);
  s.run(62, true);
  TEST_ASSERT_FALSE(s.c.valve(1).open);
  TEST_ASSERT_TRUE(s.c.valve(1).done);
}

void test_together_when_not_sequential() {
  Sim s(MON + 8 * 3600);
  s.c.cfg.sequential = false;
  s.c.setProgram(0, timerProg(0x01, 1));
  s.c.setProgram(1, timerProg(0x01, 1));
  s.run(5, true);
  TEST_ASSERT_TRUE(s.c.valve(0).open);
  TEST_ASSERT_TRUE(s.c.valve(1).open);
}

void test_close_after_water_loss_then_reopen() {
  Sim s(MON + 8 * 3600);
  s.c.cfg.v[0].closeOnWaterLoss = true;
  s.c.cfg.v[0].waterLossCloseMin = 1;
  s.c.cfg.v[1].mode = MODE_OFF;
  s.c.setProgram(0, timerProg(0x01, 30));
  s.run(10, true);
  TEST_ASSERT_TRUE(s.c.valve(0).open);
  s.run(70, false);
  TEST_ASSERT_FALSE(s.c.valve(0).open);
  TEST_ASSERT_TRUE(s.c.valve(0).waiting);
  s.run(3, true);
  TEST_ASSERT_TRUE(s.c.valve(0).open);
}

void test_bypass_opens_without_water() {
  Sim s(MON + 8 * 3600);
  s.c.cfg.v[1].mode = MODE_OFF;
  s.c.setProgram(0, timerProg(0x01, 5));
  s.in.bypass = true;
  s.run(5, false);
  TEST_ASSERT_TRUE(s.c.valve(0).open);
  TEST_ASSERT_UINT32_WITHIN(1, 4, s.c.valve(0).runSec);  // counts while bypass on
}

void test_day_result_partial_and_carry() {
  Sim s(MON + 23 * 3600 + 58 * 60);  // Monday 23:58
  Program p = timerProg(0x03, 10);   // Mon + Tue, 10 min
  p.carryOver = true;
  s.c.cfg.v[1].mode = MODE_OFF;
  s.c.setProgram(0, p);
  s.run(240, true);                  // 2 min Monday, rolls into Tuesday
  const Event* r = s.find(EV_DAY_RESULT, 0);
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL(1, r->day.status);           // partial
  TEST_ASSERT_EQUAL(10, r->day.plannedMin);
  TEST_ASSERT_UINT32_WITHIN(1, 2, r->day.runMin);
  TEST_ASSERT_TRUE(r->day.litres[0] > 15.0f);    // 2 min at 10 L/min
  TEST_ASSERT_UINT32_WITHIN(5, 8 * 60, s.c.valve(0).carrySec);
}

void test_no_water_day_result() {
  Sim s(MON + 23 * 3600 + 59 * 60);
  s.c.cfg.v[1].mode = MODE_OFF;
  s.c.setProgram(0, timerProg(0x01, 10));
  s.run(120, false);
  const Event* r = s.find(EV_DAY_RESULT, 0);
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL(2, r->day.status);           // no_water
}

void test_manual_timed_and_manual_close_stops_program() {
  Sim s(MON + 8 * 3600);
  s.c.cfg.v[0].mode = MODE_MANUAL;
  s.c.cfg.v[1].mode = MODE_OFF;
  TEST_ASSERT_TRUE(s.c.manualOpen(0, 30, 0));
  s.run(10, true);
  TEST_ASSERT_TRUE(s.c.valve(0).open);
  s.run(25, true);
  TEST_ASSERT_FALSE(s.c.valve(0).open);
  TEST_ASSERT_EQUAL(1, s.count(EV_MANUAL_DONE, 0));

  Sim p(MON + 8 * 3600);
  p.c.cfg.v[1].mode = MODE_OFF;
  p.c.setProgram(0, timerProg(0x01, 30));
  p.run(5, true);
  TEST_ASSERT_TRUE(p.c.valve(0).open);
  TEST_ASSERT_TRUE(p.c.manualClose(0));
  p.run(5, true);
  TEST_ASSERT_FALSE(p.c.valve(0).open);
  TEST_ASSERT_TRUE(p.c.valve(0).done);
}

void test_off_refuses_manual() {
  Sim s(MON);
  s.c.cfg.v[0].mode = MODE_OFF;
  TEST_ASSERT_FALSE(s.c.manualOpen(0, 60, 0));
}

void test_leak_alarm() {
  Sim s(MON + 8 * 3600);
  s.c.cfg.v[0].mode = MODE_OFF;
  s.c.cfg.v[1].mode = MODE_OFF;
  s.c.cfg.alarmDelayS = 10;
  for (int i = 0; i < 20; i++) {
    s.in.localEpoch = ++s.t;
    s.in.ps = true;
    s.in.lpm[0] = 5;
    s.c.tick(s.in);
  }
  TEST_ASSERT_TRUE(s.c.alarm(A_LEAK));
}

void test_low_flow_after_learning() {
  Sim s(MON + 6 * 3600);
  s.c.cfg.v[1].mode = MODE_OFF;
  s.c.cfg.v[0].mode = MODE_MANUAL;
  s.c.cfg.alarmDelayS = 30;
  for (int run = 0; run < 3; run++) {     // three normal 4-minute runs at 10 L/min
    s.c.manualOpen(0, 240, 0);
    s.run(250, true, 10.0f);
  }
  TEST_ASSERT_EQUAL(3, s.c.normalSamples(0));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 10.0f, s.c.normalLpm(0));
  s.c.manualOpen(0, 600, 0);
  s.run(200, true, 3.0f);                 // 30 % of normal
  TEST_ASSERT_TRUE(s.c.alarm(A_LOW_FLOW));
}

void test_next_due() {
  Sim s(MON + 10 * 3600);                      // Monday 10:00
  Program p = timerProg(0x01 | 0x04, 30);      // Mon + Wed
  p.winFrom = 6 * 60;
  p.winTo = 9 * 60;                            // 06:00-09:00, already over today
  s.c.setProgram(0, p);
  s.run(1, false);
  uint32_t due = 0;
  TEST_ASSERT_TRUE(s.c.nextDue(0, s.t, due));
  TEST_ASSERT_EQUAL_UINT32(MON + 2 * 86400 + 6 * 3600, due);  // Wednesday 06:00
  Program q = timerProg(0x01, 30);             // Monday, full day: due now
  s.c.setProgram(1, q);
  s.run(1, false);
  TEST_ASSERT_TRUE(s.c.nextDue(1, s.t, due));
  TEST_ASSERT_EQUAL_UINT32(s.t, due);
  s.c.cfg.v[1].mode = MODE_OFF;
  TEST_ASSERT_FALSE(s.c.nextDue(1, s.t, due));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_weekday);
  RUN_TEST(test_waits_for_water_then_runs_and_finishes);
  RUN_TEST(test_not_programmed_day_stays_closed);
  RUN_TEST(test_sequential_v1_then_v2);
  RUN_TEST(test_together_when_not_sequential);
  RUN_TEST(test_close_after_water_loss_then_reopen);
  RUN_TEST(test_bypass_opens_without_water);
  RUN_TEST(test_day_result_partial_and_carry);
  RUN_TEST(test_no_water_day_result);
  RUN_TEST(test_manual_timed_and_manual_close_stops_program);
  RUN_TEST(test_off_refuses_manual);
  RUN_TEST(test_leak_alarm);
  RUN_TEST(test_low_flow_after_learning);
  RUN_TEST(test_next_due);
  return UNITY_END();
}
