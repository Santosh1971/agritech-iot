// Host tests for the repeater version's path choice and child table:
//   pio test -e native
#include <unity.h>
#include "../../lib/RelayLogic/RelayLogic.h"

using namespace relay;

void setUp() {}
void tearDown() {}

static const uint8_t A[6] = {1,1,1,1,1,1}, B[6] = {2,2,2,2,2,2}, C[6] = {3,3,3,3,3,3},
                     D[6] = {4,4,4,4,4,4}, E[6] = {5,5,5,5,5,5};

void test_strong_direct_wins() {
    TEST_ASSERT_EQUAL(Path::Direct, choose(true, -70, true, -50));
}

void test_weak_direct_loses_to_better_offer() {
    TEST_ASSERT_EQUAL(Path::Relay, choose(true, -90, true, -70));
}

void test_weak_direct_kept_when_no_offer() {
    TEST_ASSERT_EQUAL(Path::Direct, choose(true, -92, false, 0));
}

void test_weak_direct_kept_when_offer_is_worse() {
    TEST_ASSERT_EQUAL(Path::Direct, choose(true, -88, true, -91));
}

void test_offer_only() {
    TEST_ASSERT_EQUAL(Path::Relay, choose(false, 0, true, -80));
    TEST_ASSERT_EQUAL(Path::None, choose(false, 0, false, 0));
}

void test_unknown_rssi_counts_as_fine() {
    TEST_ASSERT_EQUAL(Path::Direct, choose(true, 0, true, -50));     // 0 = not measured
}

void test_offer_score_is_weakest_hop() {
    TEST_ASSERT_EQUAL(-84, offerScore(-60, -84));
    TEST_ASSERT_EQUAL(-84, offerScore(-84, -60));
}

void test_switch_back_needs_margin() {
    TEST_ASSERT_FALSE(switchBackToDirect(-84));       // just above -85: stay on repeater
    TEST_ASSERT_TRUE(switchBackToDirect(-80));        // 5 dB better than the line: go direct
}

void test_child_table_add_refresh_full() {
    ChildTable t;
    TEST_ASSERT_TRUE(t.touch(A, 100));
    TEST_ASSERT_TRUE(t.touch(B, 100));
    TEST_ASSERT_TRUE(t.touch(C, 100));
    TEST_ASSERT_TRUE(t.touch(D, 100));
    TEST_ASSERT_EQUAL(4, t.count());
    TEST_ASSERT_FALSE(t.touch(E, 100));               // full
    TEST_ASSERT_TRUE(t.touch(A, 200));                // refresh an existing child is fine
    TEST_ASSERT_TRUE(t.has(A));
    TEST_ASSERT_FALSE(t.has(E));
    TEST_ASSERT_FALSE(t.canAccept(E));
    TEST_ASSERT_TRUE(t.canAccept(B));
}

void test_child_table_expiry() {
    ChildTable t;
    t.touch(A, 1000);
    t.touch(B, 50000);
    t.expire(1000 + ChildTable::EXPIRE_MS + 1);       // A silent too long
    TEST_ASSERT_FALSE(t.has(A));
    TEST_ASSERT_TRUE(t.has(B));
    TEST_ASSERT_EQUAL(1, t.count());
    TEST_ASSERT_TRUE(t.touch(E, 400000));             // its slot is free again
}

void test_child_table_millis_wrap() {
    ChildTable t;
    t.touch(A, 0xFFFFFF00u);
    t.expire(0x00000100u);                            // 512 ms later, across the wrap
    TEST_ASSERT_TRUE(t.has(A));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_strong_direct_wins);
    RUN_TEST(test_weak_direct_loses_to_better_offer);
    RUN_TEST(test_weak_direct_kept_when_no_offer);
    RUN_TEST(test_weak_direct_kept_when_offer_is_worse);
    RUN_TEST(test_offer_only);
    RUN_TEST(test_unknown_rssi_counts_as_fine);
    RUN_TEST(test_offer_score_is_weakest_hop);
    RUN_TEST(test_switch_back_needs_margin);
    RUN_TEST(test_child_table_add_refresh_full);
    RUN_TEST(test_child_table_expiry);
    RUN_TEST(test_child_table_millis_wrap);
    return UNITY_END();
}
