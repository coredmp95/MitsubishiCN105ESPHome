/// test_cycle_management.cpp — Characterization of cycle_management.cpp timing
/// Role: pins today's polling-cycle timing (interval, cycle timeout, deferral) and the millis() wrap
///       stall (audit BUG-01), known bugs included, before Phase 4 (TIME-01/TIME-02) changes them.
/// Deps: cycle_management.h, fixture.h, test_platform.h

#include "fixture.h"

#include "cycle_management.h"

// ════════════════════════════════════════════════════════════════
// CycleManagement — struct level, the test clock is set explicitly
// ════════════════════════════════════════════════════════════════

class CycleManagement : public cn105_test::ClockFixture {};

// Pins current buggy behaviour — flip in Phase 4 (TIME-01)
// Mechanism (AUDIT BUG-01): the end of the last cycle is stored as an absolute 32-bit millis() value and
// compared with "now < last => false". After millis() wraps, now is small and last is close to 2^32, so
// the interval never elapses until millis() catches up again (about 49.7 days).
TEST_F(CycleManagement, Characterize_BUG01_StallsAfterMillisWrap) {
    cn105_test::TestClock::set(0xFFFFFF00ull);
    cycleManagement cycle;
    cycle.init();
    cycle.cycleStarted();
    cn105_test::TestClock::advance(100);
    cycle.cycleEnded();
    ASSERT_EQ(cycle.lastCompleteCycleMs, 0xFFFFFF64ul);

    // 2^32 + 0x800: esphome::millis() == 2048, 2.3 s after the cycle ended, far beyond the 1000 ms interval.
    cn105_test::TestClock::set(0x100000000ull + 0x800);
    ASSERT_EQ(cn105_test::TestClock::now(), 2048u);
    EXPECT_FALSE(cycle.hasUpdateIntervalPassed(1000));

    // Still stalled a minute later: no new polling cycle would ever start.
    cn105_test::TestClock::advance(60000);
    EXPECT_FALSE(cycle.hasUpdateIntervalPassed(1000));
}
