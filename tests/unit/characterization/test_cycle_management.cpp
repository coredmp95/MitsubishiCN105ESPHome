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

TEST_F(CycleManagement, Characterize_InitClearsRunningAndStampsNow) {
    cn105_test::TestClock::set(5000);
    cycleManagement cycle;
    cycle.cycleRunning = true;
    cycle.lastCycleStartMs = 1234;

    cycle.init();

    EXPECT_FALSE(cycle.isCycleRunning());
    EXPECT_EQ(cycle.lastCompleteCycleMs, 5000ul);
    // init() does not touch the start stamp.
    EXPECT_EQ(cycle.lastCycleStartMs, 1234ul);
}

// ════════════════════════════════════════════════════════════════
// Interval and timeout boundaries (strict '>', RESEARCH Pitfall 6)
// ════════════════════════════════════════════════════════════════

TEST_F(CycleManagement, Characterize_HasUpdateIntervalPassedIsStrict) {
    cn105_test::TestClock::set(10000);
    cycleManagement cycle;
    cycle.init();  // lastCompleteCycleMs == 10000

    cn105_test::TestClock::set(10000 + 1000);
    EXPECT_FALSE(cycle.hasUpdateIntervalPassed(1000)) << "at last + interval the interval has not passed (strict '>')";
    cn105_test::TestClock::set(10000 + 1001);
    EXPECT_TRUE(cycle.hasUpdateIntervalPassed(1000)) << "at last + interval + 1 it has";

    // A clock behind the stamp is "not passed" whatever the interval (the guard against unsigned underflow).
    cn105_test::TestClock::set(9999);
    EXPECT_FALSE(cycle.hasUpdateIntervalPassed(0));
}

TEST_F(CycleManagement, Characterize_CycleTimeoutBoundary) {
    const uint64_t started = 20000;
    cn105_test::TestClock::set(started);
    cycleManagement cycle;
    cycle.init();
    cycle.cycleStarted();
    ASSERT_TRUE(cycle.isCycleRunning());
    EXPECT_EQ(cycle.lastCycleStartMs, 20000ul);

    // Timeout is strictly greater than 2 * interval + 1000 = 3000 ms for a 1000 ms interval.
    cn105_test::TestClock::set(started + 3000);
    EXPECT_FALSE(cycle.doesCycleTimeOut(1000));
    cn105_test::TestClock::set(started + 3001);
    EXPECT_TRUE(cycle.doesCycleTimeOut(1000));

    // A clock behind the start stamp never times out (underflow guard).
    cn105_test::TestClock::set(started - 1);
    EXPECT_FALSE(cycle.doesCycleTimeOut(1000));
}

TEST_F(CycleManagement, Characterize_CheckTimeoutEndsCycleAndLogs) {
    cn105_test::TestClock::set(30000);
    cycleManagement cycle;
    cycle.init();
    cycle.cycleStarted();

    cn105_test::TestClock::set(30000 + 3000);
    cycle.checkTimeout(1000);
    EXPECT_TRUE(cycle.isCycleRunning()) << "not timed out yet at 2 * interval + 1000";
    EXPECT_FALSE(cn105_test::TestLog::contains("Cycle timeout"));

    cn105_test::TestClock::set(30000 + 3001);
    cycle.checkTimeout(1000);
    EXPECT_FALSE(cycle.isCycleRunning());
    EXPECT_TRUE(cn105_test::TestLog::contains("Cycle timeout"));
    // A timed-out cycle ends like a complete one: the interval restarts from the timeout.
    EXPECT_EQ(cycle.lastCompleteCycleMs, 33001ul);
    EXPECT_TRUE(cn105_test::TestLog::contains("with timeout?: YES"));
}

// ════════════════════════════════════════════════════════════════
// Deferral
// ════════════════════════════════════════════════════════════════

TEST_F(CycleManagement, Characterize_DeferCycleSetsDeadline1500ms) {
    cn105_test::TestClock::set(40000);
    cycleManagement cycle;
    cycle.init();

    cycle.deferCycle();

    // 2 * DEFER_SCHEDULE_UPDATE_LOOP_DELAY (750): the harness compiles at ESPHOME_LOG_LEVEL_VERY_VERBOSE, so
    // the 750 ms INFO-level branch of deferCycle() is not compiled in this build (D-22).
    EXPECT_EQ(cycle.lastCompleteCycleMs, 41500ul);
    EXPECT_TRUE(cn105_test::TestLog::contains("Defering cycle trigger"));

    // The next cycle may only start strictly more than interval after that deadline.
    cn105_test::TestClock::set(41500 + 1000);
    EXPECT_FALSE(cycle.hasUpdateIntervalPassed(1000));
    cn105_test::TestClock::set(41500 + 1001);
    EXPECT_TRUE(cycle.hasUpdateIntervalPassed(1000));
}

TEST_F(CycleManagement, Characterize_CycleEndedKeepsFutureDeadline) {
    cn105_test::TestClock::set(50000);
    cycleManagement cycle;
    cycle.init();
    cycle.cycleStarted();
    cycle.deferCycle();  // deadline 51500
    ASSERT_EQ(cycle.lastCompleteCycleMs, 51500ul);

    cn105_test::TestClock::advance(500);
    cycle.cycleEnded();

    EXPECT_FALSE(cycle.isCycleRunning());
    EXPECT_EQ(cycle.lastCompleteCycleMs, 51500ul) << "a defer survives cycleEnded() while it is in the future";

    // Once the deadline is in the past, cycleEnded() stamps the end of the cycle again.
    cycle.cycleStarted();
    cn105_test::TestClock::set(60000);
    cycle.cycleEnded();
    EXPECT_EQ(cycle.lastCompleteCycleMs, 60000ul);
}

// ════════════════════════════════════════════════════════════════
// BUG-01 — absolute 32-bit stamps break across the millis() wrap
// ════════════════════════════════════════════════════════════════

// Pins current buggy behaviour — flip in Phase 4 (TIME-02)
// Mechanism (AUDIT BUG-01): doesCycleTimeOut() answers false whenever now < lastCycleStartMs, which is the
// case for the whole time between the wrap and millis() catching up with the start stamp.
TEST_F(CycleManagement, Characterize_BUG01_CycleNeverTimesOutAcrossWrap) {
    cn105_test::TestClock::set(0xFFFFFF00ull);
    cycleManagement cycle;
    cycle.init();
    cycle.cycleStarted();
    ASSERT_EQ(cycle.lastCycleStartMs, 0xFFFFFF00ul);

    // 16 s after the start (the cycle timeout for a 1000 ms interval is 3 s), 15.7 s after the wrap.
    cn105_test::TestClock::set(0x100000000ull + 16000 - 0x100);
    ASSERT_EQ(cn105_test::TestClock::now(), 15744u);
    EXPECT_FALSE(cycle.doesCycleTimeOut(1000));

    // checkTimeout() therefore leaves the stuck cycle running.
    cycle.checkTimeout(1000);
    EXPECT_TRUE(cycle.isCycleRunning());
    EXPECT_FALSE(cn105_test::TestLog::contains("Cycle timeout"));
}

// Pins current buggy behaviour — flip in Phase 4 (TIME-02)
// Mechanism (AUDIT BUG-01): deferCycle() stores now + 1500 as an absolute 32-bit value. Called 256 ms before
// the wrap, the deadline itself wraps to 1244, so it lies in the past of the pre-wrap clock and the
// deferral is lost.
TEST_F(CycleManagement, Characterize_BUG01_DeferJustBeforeWrap) {
    cn105_test::TestClock::set(0xFFFFFF00ull);
    cycleManagement cycle;
    cycle.init();

    cycle.deferCycle();

    EXPECT_EQ(cycle.lastCompleteCycleMs, 1244ul) << "0xFFFFFF00 + 1500 wrapped to 32 bits";
    cn105_test::TestClock::advance(1);
    EXPECT_TRUE(cycle.hasUpdateIntervalPassed(1000)) << "the defer is lost: the next cycle may start 1 ms later";

    // After the wrap the same stamp is a deadline in the future again, so polling is held back again
    // until millis() reaches 1244 + 1000 (the deferral reappears, with a delay of 256 ms + 2244 ms).
    cn105_test::TestClock::set(0x100000000ull + 1244 + 1000);
    EXPECT_FALSE(cycle.hasUpdateIntervalPassed(1000));
    cn105_test::TestClock::set(0x100000000ull + 1244 + 1001);
    EXPECT_TRUE(cycle.hasUpdateIntervalPassed(1000));
}

// ════════════════════════════════════════════════════════════════
// CycleManagementComponent — the real CN105Climate loop across a wrap
// ════════════════════════════════════════════════════════════════

class CycleManagementComponent : public cn105_test::Cn105Fixture {
   protected:
    cn105_test::Cn105Options options() const override {
        cn105_test::Cn105Options opts;
        opts.start_ms = 0xFFFF0000ull;  // 65.5 s before millis() wraps
        return opts;
    }

    /// Steps in 10 ms ticks for total_ms, answering every info request from default_responses().
    /// Returns the (test-clock ms, info code) of each FC 42 request the component wrote.
    std::vector<std::pair<uint64_t, uint8_t>> pump(uint32_t total_ms) {
        std::vector<std::pair<uint64_t, uint8_t>> requests;
        const auto responses = default_responses();
        for (uint32_t elapsed = 0; elapsed < total_ms; elapsed += 10) {
            step(10);
            for (const auto& frame : take_tx_frames()) {
                if (frame.size() < 6 || frame[1] != 0x42) continue;
                requests.emplace_back(cn105_test::TestClock::now64(), frame[5]);
                auto it = responses.find(frame[5]);
                if (it != responses.end()) feed(it->second);
            }
        }
        return requests;
    }
};

TEST_F(CycleManagementComponent, Characterize_NextCycleStartsAfterInterval) {
    // Control for the wrap test: before the wrap, a new cycle starts about update_interval after the last.
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();

    const auto requests = pump(8000);
    ASSERT_LT(cn105_test::TestClock::now64(), 0x100000000ull) << "the control must stay before the wrap";

    std::vector<uint64_t> cycle_starts;
    for (const auto& r : requests) {
        if (r.second == 0x02) cycle_starts.push_back(r.first);
    }
    ASSERT_GE(cycle_starts.size(), 3u);
    // Observed with the unmodified component: the gap between two cycle starts (one poll cycle answered
    // immediately plus the strict-greater interval and the 10 ms tick granularity).
    for (size_t i = 1; i < cycle_starts.size(); i++) {
        const uint64_t gap = cycle_starts[i] - cycle_starts[i - 1];
        EXPECT_GT(gap, 1000u);
        EXPECT_LE(gap, 1100u);
    }
}

// Pins current buggy behaviour — flip in Phase 4 (TIME-01)
// Mechanism (AUDIT BUG-01): see CycleManagement.Characterize_BUG01_StallsAfterMillisWrap. Here the real
// loop() stops starting polling cycles once millis() has wrapped, although the heat pump keeps answering.
TEST_F(CycleManagementComponent, Characterize_BUG01_ComponentStopsPollingAfterWrap) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();

    const uint64_t wrap = 0x100000000ull;
    // Poll normally until the clock is just before the wrap.
    uint32_t guard = 0;
    size_t cycles_before_wrap = 0;
    while (cn105_test::TestClock::now64() < wrap - 20 && guard++ < 10000) {
        for (const auto& r : pump(10)) {
            if (r.second == 0x02) cycles_before_wrap++;
        }
    }
    EXPECT_GE(cycles_before_wrap, 30u) << "about 55 s of polling at a 1 s interval before the wrap";
    ASSERT_LT(cn105_test::TestClock::now64(), wrap);
    const unsigned long last_complete_before_wrap = hp().loopCycle.lastCompleteCycleMs;
    EXPECT_GT(last_complete_before_wrap, 0xFFFF0000ul) << "polling worked until the wrap";

    // Cross the wrap and keep answering for another minute.
    const auto after = pump(60000);
    ASSERT_GE(cn105_test::TestClock::now64(), wrap + 60000 - 20);

    std::vector<uint64_t> late_cycle_starts;
    for (const auto& r : after) {
        if (r.first >= wrap + 1500 && r.second == 0x02) late_cycle_starts.push_back(r.first);
    }
    EXPECT_TRUE(late_cycle_starts.empty()) << "no new polling cycle starts after the wrap";
    EXPECT_FALSE(hp().loopCycle.isCycleRunning());
    EXPECT_FALSE(hp().loopCycle.hasUpdateIntervalPassed(1000));
}
