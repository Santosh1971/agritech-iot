// Host tests for the misting rules:  pio test -e native
#include <unity.h>
#include "MistLogic.h"
#include "../../lib/MistLogic/MistLogic.cpp"

using namespace mist;

static Controller c;
static Inputs in;

static void reset() {
    c = Controller();
    c.setSettings(Settings());          // 32/29, 45/60, 120/180, 11:00-15:00
    in = Inputs();
    in.sensorOk = true;
    in.timeValid = true;
    in.minuteOfDay = 12 * 60;           // noon, inside the window
    in.nowSec = 1000;                   // well past the boot hold
    in.t = 30; in.rh = 50;
}

static bool step(float t, float rh, uint32_t advance = 1) {
    in.nowSec += advance;
    in.t = t; in.rh = rh;
    return c.update(in);
}

void setUp() { reset(); }
void tearDown() {}

void test_hot_starts_misting() {
    TEST_ASSERT_TRUE(step(32.0f, 50));
    TEST_ASSERT_EQUAL(Reason::PulseOn, c.reason());
}

void test_band_does_not_start() {
    TEST_ASSERT_FALSE(step(31.0f, 50));
    TEST_ASSERT_EQUAL(Reason::Idle, c.reason());
}

void test_dry_and_warm_starts() {
    TEST_ASSERT_TRUE(step(29.5f, 45));            // RH <= 45 and T > 29
}

void test_dry_but_cool_does_not_start() {
    TEST_ASSERT_FALSE(step(29.0f, 40));           // T <= 29 is a stop
    TEST_ASSERT_EQUAL(Reason::StopTempLow, c.reason());
}

void test_rh_high_wins_over_hot() {
    TEST_ASSERT_FALSE(step(35.0f, 60));
    TEST_ASSERT_EQUAL(Reason::StopRhHigh, c.reason());
}

void test_hysteresis_keeps_misting_in_band() {
    step(33, 50);                                  // start
    TEST_ASSERT_TRUE(c.demand());
    step(30.5f, 55, 10);                           // cooled into the band
    TEST_ASSERT_TRUE(c.demand());
    TEST_ASSERT_TRUE(c.pumpOn());
    step(29.0f, 55, 10);                           // reached stop
    TEST_ASSERT_FALSE(c.demand());
    TEST_ASSERT_FALSE(c.pumpOn());
}

void test_pulse_2_on_3_off() {
    TEST_ASSERT_TRUE(step(33, 50));
    TEST_ASSERT_TRUE(step(33, 50, 119));           // 119 s on
    TEST_ASSERT_FALSE(step(33, 50, 1));            // 120 s -> off
    TEST_ASSERT_EQUAL(Reason::PulseGap, c.reason());
    TEST_ASSERT_FALSE(step(33, 50, 179));          // still resting
    TEST_ASSERT_TRUE(step(33, 50, 1));             // 180 s -> on again
}

void test_stop_mid_pulse_is_immediate() {
    step(33, 50);
    TEST_ASSERT_FALSE(step(33, 61, 30));
    TEST_ASSERT_EQUAL(Reason::StopRhHigh, c.reason());
}

void test_min_off_applies_after_a_stop() {
    step(33, 50);
    step(33, 61, 30);                              // stopped by RH
    TEST_ASSERT_FALSE(step(33, 50, 60));           // hot again but only 60 s rest
    TEST_ASSERT_EQUAL(Reason::PulseGap, c.reason());
    TEST_ASSERT_TRUE(step(33, 50, 120));           // 180 s rest done
}

void test_window() {
    in.minuteOfDay = 10 * 60 + 59;
    TEST_ASSERT_FALSE(step(35, 40));
    TEST_ASSERT_EQUAL(Reason::OutsideWindow, c.reason());
    in.minuteOfDay = 11 * 60;
    TEST_ASSERT_TRUE(step(35, 40));
    in.minuteOfDay = 15 * 60;                      // end is exclusive
    TEST_ASSERT_FALSE(step(35, 40));
}

void test_window_ignored_when_clock_not_set() {
    in.timeValid = false;
    in.minuteOfDay = 2 * 60;
    TEST_ASSERT_TRUE(step(35, 40));
}

void test_window_wraps_midnight() {
    TEST_ASSERT_TRUE(Controller::minuteInWindow(23 * 60, 22 * 60, 2 * 60));
    TEST_ASSERT_TRUE(Controller::minuteInWindow(60, 22 * 60, 2 * 60));
    TEST_ASSERT_FALSE(Controller::minuteInWindow(12 * 60, 22 * 60, 2 * 60));
}

void test_sensor_fault_stops() {
    step(33, 50);
    in.sensorOk = false;
    TEST_ASSERT_FALSE(step(33, 50, 5));
    TEST_ASSERT_EQUAL(Reason::SensorFault, c.reason());
}

void test_rh_stop_needs_margin_to_restart() {
    TEST_ASSERT_TRUE(step(33, 50));                // misting
    TEST_ASSERT_FALSE(step(33, 60.0f));            // RH hits 60: stop at once
    TEST_ASSERT_EQUAL(Reason::StopRhHigh, c.reason());
    TEST_ASSERT_FALSE(step(33, 59.8f, 400));       // just under 60, rest done: still stopped
    TEST_ASSERT_EQUAL(Reason::StopRhHigh, c.reason());
    TEST_ASSERT_FALSE(step(33, 57.1f, 10));        // not yet 3 % below
    TEST_ASSERT_EQUAL(Reason::StopRhHigh, c.reason());
    TEST_ASSERT_TRUE(step(33, 57.0f, 10));         // 57 = 60 - 3: misting again
    TEST_ASSERT_EQUAL(Reason::PulseOn, c.reason());
}

void test_rh_margin_only_after_a_rh_stop() {
    TEST_ASSERT_TRUE(step(33, 59.0f));             // never reached 60: starts as before
}

void test_boot_hold() {
    c = Controller();
    in.nowSec = 10;                                // 10 s after power-on
    TEST_ASSERT_FALSE(step(35, 40, 0));
    TEST_ASSERT_EQUAL(Reason::PulseGap, c.reason());
    in.nowSec = 180;
    TEST_ASSERT_TRUE(step(35, 40, 0));
}

void test_start_limit() {
    Settings s; s.onSec = 5; s.offSec = 30; s.maxStartsHr = 3;
    c.setSettings(s);
    int starts = 0;
    bool prev = false;
    for (int i = 0; i < 600; i++) {                // 10 minutes hot
        bool on = step(35, 40);
        if (on && !prev) starts++;
        prev = on;
    }
    TEST_ASSERT_EQUAL(3, starts);
    TEST_ASSERT_EQUAL(Reason::StartLimit, c.reason());
}

void test_manual_and_timeout() {
    c.manualOn(in.nowSec);
    TEST_ASSERT_TRUE(step(25, 80));                // manual ignores climate
    TEST_ASSERT_EQUAL(Mode::Manual, c.mode());
    TEST_ASSERT_FALSE(step(25, 80, 30 * 60));      // 30 min timeout
    TEST_ASSERT_EQUAL(Mode::Auto, c.mode());
}

void test_mode_off() {
    c.setMode(Mode::Off, in.nowSec);
    TEST_ASSERT_FALSE(step(40, 20));
    TEST_ASSERT_EQUAL(Reason::ModeOff, c.reason());
}

void test_sanitize() {
    Settings s; s.tStart = 20; s.tStop = 29; s.offSec = 2; s.rhHigh = 30; s.rhLow = 45;
    TEST_ASSERT_FALSE(s.sanitize());
    TEST_ASSERT_TRUE(s.tStart > s.tStop);
    TEST_ASSERT_EQUAL(30, s.offSec);
    TEST_ASSERT_TRUE(s.rhHigh > s.rhLow);
}

void test_rolling_avg() {
    RollingAvg a;
    a.add(30, 100); a.add(32, 110); a.add(34, 120);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 32, a.value(120));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 33, a.value(165));   // first sample aged out
    TEST_ASSERT_TRUE(a.valid(200, 120));
    TEST_ASSERT_FALSE(a.valid(300, 120));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hot_starts_misting);
    RUN_TEST(test_band_does_not_start);
    RUN_TEST(test_dry_and_warm_starts);
    RUN_TEST(test_dry_but_cool_does_not_start);
    RUN_TEST(test_rh_high_wins_over_hot);
    RUN_TEST(test_rh_stop_needs_margin_to_restart);
    RUN_TEST(test_rh_margin_only_after_a_rh_stop);
    RUN_TEST(test_hysteresis_keeps_misting_in_band);
    RUN_TEST(test_pulse_2_on_3_off);
    RUN_TEST(test_stop_mid_pulse_is_immediate);
    RUN_TEST(test_min_off_applies_after_a_stop);
    RUN_TEST(test_window);
    RUN_TEST(test_window_ignored_when_clock_not_set);
    RUN_TEST(test_window_wraps_midnight);
    RUN_TEST(test_sensor_fault_stops);
    RUN_TEST(test_boot_hold);
    RUN_TEST(test_start_limit);
    RUN_TEST(test_manual_and_timeout);
    RUN_TEST(test_mode_off);
    RUN_TEST(test_sanitize);
    RUN_TEST(test_rolling_avg);
    return UNITY_END();
}
