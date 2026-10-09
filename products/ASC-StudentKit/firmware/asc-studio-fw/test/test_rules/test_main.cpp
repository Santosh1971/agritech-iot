#include <unity.h>
#include "rules.h"

static Rule pump() {
  Rule r = {"OUT1", "S1", true, 30, 45, 6 * 60, 18 * 60, ""};
  return r;
}

void setUp() {}
void tearDown() {}

void test_turns_on_below_and_holds_until_off_level() {
  Rule r = pump();
  int noon = 12 * 60;
  TEST_ASSERT_FALSE(evaluateRule(r, false, 31, noon, -1));
  TEST_ASSERT_TRUE(evaluateRule(r, false, 29, noon, -1));
  TEST_ASSERT_TRUE(evaluateRule(r, true, 40, noon, -1));   // hysteresis: stays on
  TEST_ASSERT_FALSE(evaluateRule(r, true, 45, noon, -1));  // reaches the OFF level
}

void test_outside_window_and_unknown_time_are_off() {
  Rule r = pump();
  TEST_ASSERT_FALSE(evaluateRule(r, false, 10, 20 * 60, -1));
  TEST_ASSERT_FALSE(evaluateRule(r, true, 10, -1, -1));
}

void test_window_across_midnight() {
  Rule r = pump();
  r.fromMin = 22 * 60; r.toMin = 2 * 60;
  TEST_ASSERT_TRUE(evaluateRule(r, false, 10, 23 * 60, -1));
  TEST_ASSERT_TRUE(evaluateRule(r, false, 10, 60, -1));
  TEST_ASSERT_FALSE(evaluateRule(r, false, 10, 12 * 60, -1));
}

void test_failed_sensor_is_off() {
  Rule r = pump();
  TEST_ASSERT_FALSE(evaluateRule(r, true, NAN, 12 * 60, -1));
}

void test_guard_must_be_safe() {
  Rule r = pump();
  strcpy(r.guard, "S2");
  TEST_ASSERT_FALSE(evaluateRule(r, false, 10, 12 * 60, 0));
  TEST_ASSERT_FALSE(evaluateRule(r, false, 10, 12 * 60, -1));
  TEST_ASSERT_TRUE(evaluateRule(r, false, 10, 12 * 60, 1));
}

void test_above_rule_for_fogger() {
  Rule r = {"OUT2", "I2C-1:t", false, 35, 33, -1, -1, ""};
  TEST_ASSERT_FALSE(evaluateRule(r, false, 34, -1, -1));  // no window: time not needed
  TEST_ASSERT_TRUE(evaluateRule(r, false, 36, -1, -1));
  TEST_ASSERT_TRUE(evaluateRule(r, true, 34, -1, -1));
  TEST_ASSERT_FALSE(evaluateRule(r, true, 33, -1, -1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_turns_on_below_and_holds_until_off_level);
  RUN_TEST(test_outside_window_and_unknown_time_are_off);
  RUN_TEST(test_window_across_midnight);
  RUN_TEST(test_failed_sensor_is_off);
  RUN_TEST(test_guard_must_be_safe);
  RUN_TEST(test_above_rule_for_fogger);
  return UNITY_END();
}
