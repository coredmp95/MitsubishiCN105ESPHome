/// test_request_scheduler.cpp — Characterization of request_scheduler.cpp (the info-request chain)
/// Role: pins today's RequestScheduler behaviour standalone (recording callbacks) and through the real
///       CN105Climate (registered request table, stalls), known bugs included (audit BUG-07), before
///       Phase 5 (LINK-03/LINK-04) changes them.
/// Deps: request_scheduler.h, info_request.h, cn105.h, fixture.h, test_platform.h

#include "fixture.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "hvac_option_switch.h"
#include "info_request.h"
#include "request_scheduler.h"

// ════════════════════════════════════════════════════════════════
// Standalone scheduler with recording callbacks
// ════════════════════════════════════════════════════════════════

namespace {

/// Records what the scheduler asks its owner to do. The context callback returns nullptr, so canSend and
/// onResponse are skipped (pinned by Characterize_NullContextSkipsCanSendAndOnResponse).
struct Recorder {
    struct ScheduledTimeout {
        std::string name;
        uint32_t ms;
        std::function<void()> fire;
    };

    std::vector<uint8_t> sent;
    std::vector<ScheduledTimeout> timeouts;
    int terminated = 0;

    esphome::RequestScheduler make() {
        return esphome::RequestScheduler(
            [this](uint8_t code) { sent.push_back(code); },
            [this](const std::string& name, uint32_t ms, std::function<void()> fire) {
                timeouts.push_back({name, ms, std::move(fire)});
            },
            [this]() { terminated++; },
            []() -> esphome::CN105Climate* { return nullptr; });
    }

    void clear() {
        sent.clear();
        timeouts.clear();
        terminated = 0;
    }
};

/// Registers a request with the given parameters (the scheduler copies it).
void add(esphome::RequestScheduler& sched, uint8_t code, uint8_t max_failures = 3, uint32_t soft_timeout_ms = 0,
         uint32_t interval_ms = 0) {
    esphome::InfoRequest req("id", "Request", code, max_failures, soft_timeout_ms, interval_ms);
    sched.register_request(req);
}

const esphome::InfoRequest* find(const esphome::RequestScheduler& sched, uint8_t code) {
    for (const auto& r : sched.requests_) {
        if (r.code == code) return &r;
    }
    return nullptr;
}

}  // namespace

class RequestScheduler : public cn105_test::ClockFixture {
   protected:
    Recorder rec_;
};

using Codes = std::vector<uint8_t>;

// ════════════════════════════════════════════════════════════════
// Chain order and skips
// ════════════════════════════════════════════════════════════════

TEST_F(RequestScheduler, Characterize_SendNextAfterUnknownCodeStartsAtFirst) {
    auto sched = rec_.make();
    add(sched, 0x02);
    add(sched, 0x03);

    sched.send_next_after(0x00);
    EXPECT_EQ(rec_.sent, (Codes{0x02}));

    // Any code the scheduler does not know starts from the beginning too (start index -1 -> 0).
    rec_.clear();
    sched.send_next_after(0xEE);
    EXPECT_EQ(rec_.sent, (Codes{0x02}));
    EXPECT_EQ(rec_.terminated, 0);
}

TEST_F(RequestScheduler, Characterize_RegistrationOrderIsCycleOrder) {
    auto sched = rec_.make();
    add(sched, 0x06);
    add(sched, 0x02);
    add(sched, 0x03);

    sched.send_next_after(0x00);
    EXPECT_TRUE(sched.process_response(0x06));
    EXPECT_TRUE(sched.process_response(0x02));
    EXPECT_EQ(rec_.terminated, 0);
    EXPECT_TRUE(sched.process_response(0x03));

    // Registration order, not code order.
    EXPECT_EQ(rec_.sent, (Codes{0x06, 0x02, 0x03}));
    EXPECT_EQ(rec_.terminated, 1) << "the response of the last request ends the cycle";
}

TEST_F(RequestScheduler, Characterize_DisabledRequestIsSkippedUntilEnabled) {
    auto sched = rec_.make();
    add(sched, 0x02);
    add(sched, 0x03);
    add(sched, 0x06);

    sched.disable_request(0x03);
    sched.disable_request(0x77);  // unknown code: silently ignored
    sched.send_next_after(0x00);
    sched.process_response(0x02);
    EXPECT_EQ(rec_.sent, (Codes{0x02, 0x06}));

    rec_.clear();
    sched.enable_request(0x03);
    sched.send_next_after(0x00);
    sched.process_response(0x02);
    EXPECT_EQ(rec_.sent, (Codes{0x02, 0x03}));
}

TEST_F(RequestScheduler, Characterize_IntervalSkip) {
    auto sched = rec_.make();
    add(sched, 0x02);
    add(sched, 0x20, 3, 0, 5000);  // at most one request per 5 s

    cn105_test::TestClock::set(10000);
    sched.send_next_after(0x00);
    sched.process_response(0x02);
    ASSERT_EQ(rec_.sent, (Codes{0x02, 0x20}));
    ASSERT_EQ(find(sched, 0x20)->last_request_time, 10000u);

    // While elapsed < interval the request is skipped and the chain ends instead.
    rec_.clear();
    cn105_test::TestClock::set(14999);
    sched.send_next_after(0x02);
    EXPECT_TRUE(rec_.sent.empty());
    EXPECT_EQ(rec_.terminated, 1);

    // The skip condition is strict '<': at elapsed == interval the request is sent again.
    rec_.clear();
    cn105_test::TestClock::set(15000);
    sched.send_next_after(0x02);
    EXPECT_EQ(rec_.sent, (Codes{0x20}));
    EXPECT_EQ(rec_.terminated, 0);
}

TEST_F(RequestScheduler, Characterize_RequestSentAtMillisZeroLooksNeverSent) {
    auto sched = rec_.make();
    add(sched, 0x20, 3, 0, 5000);

    // last_request_time == 0 doubles as "never sent": a request genuinely sent at millis() == 0 is not
    // throttled by its interval afterwards.
    cn105_test::TestClock::set(0);
    sched.send_next_after(0x00);
    ASSERT_EQ(rec_.sent, (Codes{0x20}));
    EXPECT_EQ(find(sched, 0x20)->last_request_time, 0u);

    rec_.clear();
    cn105_test::TestClock::set(100);
    sched.send_next_after(0x00);
    EXPECT_EQ(rec_.sent, (Codes{0x20})) << "sent again 100 ms later despite the 5000 ms interval";
}

TEST_F(RequestScheduler, Characterize_TimerBypassResetsLastRequestTime) {
    auto sched = rec_.make();
    add(sched, 0x20, 3, 0, 5000);

    cn105_test::TestClock::set(10000);
    sched.send_next_after(0x00);
    ASSERT_EQ(find(sched, 0x20)->last_request_time, 10000u);

    rec_.clear();
    cn105_test::TestClock::set(10100);
    sched.send_next_after(0x00);
    ASSERT_TRUE(rec_.sent.empty()) << "throttled by the interval";

    sched.timer_bypass(0x20);
    sched.timer_bypass(0x77);  // unknown code: silently ignored
    EXPECT_EQ(find(sched, 0x20)->last_request_time, 0u);

    rec_.clear();
    sched.send_next_after(0x00);
    EXPECT_EQ(rec_.sent, (Codes{0x20}));
}

TEST_F(RequestScheduler, Characterize_TerminateWhenListExhausted) {
    auto sched = rec_.make();
    EXPECT_TRUE(sched.is_empty());
    sched.send_next_after(0x00);
    EXPECT_TRUE(rec_.sent.empty());
    EXPECT_EQ(rec_.terminated, 1) << "an empty list ends the cycle immediately";

    rec_.clear();
    add(sched, 0x02);
    EXPECT_FALSE(sched.is_empty());
    sched.disable_request(0x02);
    sched.send_next_after(0x00);
    EXPECT_TRUE(rec_.sent.empty());
    EXPECT_EQ(rec_.terminated, 1) << "so does a list whose requests are all disabled";

    sched.clear_requests();
    EXPECT_TRUE(sched.is_empty());
}

TEST_F(RequestScheduler, Characterize_ProcessResponseUnknownCodeReturnsFalse) {
    auto sched = rec_.make();
    add(sched, 0x02);
    add(sched, 0x03);

    EXPECT_FALSE(sched.process_response(0x66));
    EXPECT_TRUE(rec_.sent.empty());
    EXPECT_EQ(rec_.terminated, 0);
}

TEST_F(RequestScheduler, Characterize_NullContextSkipsCanSendAndOnResponse) {
    auto sched = rec_.make();
    int can_send_calls = 0;
    int on_response_calls = 0;
    esphome::InfoRequest req("id", "Request", 0x42, 3, 0);
    req.canSend = [&can_send_calls](const esphome::CN105Climate&) {
        can_send_calls++;
        return false;
    };
    req.onResponse = [&on_response_calls](esphome::CN105Climate&) { on_response_calls++; };
    sched.register_request(req);

    // The context callback yields nullptr: canSend is not consulted (the request is sent although its
    // canSend would say no) and onResponse is not invoked.
    sched.send_next_after(0x00);
    EXPECT_EQ(rec_.sent, (Codes{0x42}));
    EXPECT_TRUE(sched.process_response(0x42));
    EXPECT_EQ(can_send_calls, 0);
    EXPECT_EQ(on_response_calls, 0);
    EXPECT_EQ(rec_.terminated, 1);
}

// ════════════════════════════════════════════════════════════════
// Soft timeouts
// ════════════════════════════════════════════════════════════════

TEST_F(RequestScheduler, Characterize_SoftTimeoutCountsFailureAndContinues) {
    auto sched = rec_.make();
    add(sched, 0x02);
    add(sched, 0x09, 3, 500);
    add(sched, 0x04);

    sched.send_next_after(0x02);
    ASSERT_EQ(rec_.sent, (Codes{0x09}));
    ASSERT_EQ(rec_.timeouts.size(), 1u);
    EXPECT_EQ(rec_.timeouts[0].name, "info_timeout_0x09");
    EXPECT_EQ(rec_.timeouts[0].ms, 500u);
    EXPECT_TRUE(find(sched, 0x09)->awaiting);

    // Firing the timeout is a soft failure: awaiting cleared, failure counted, the chain moves on.
    rec_.timeouts[0].fire();
    EXPECT_FALSE(find(sched, 0x09)->awaiting);
    EXPECT_EQ(find(sched, 0x09)->failures, 1);
    EXPECT_EQ(rec_.sent, (Codes{0x09, 0x04}));
    EXPECT_TRUE(cn105_test::TestLog::contains("Soft timeout for"));
    EXPECT_FALSE(find(sched, 0x09)->disabled);

    // Second failure.
    rec_.clear();
    sched.send_next_after(0x02);
    rec_.timeouts.back().fire();
    EXPECT_EQ(find(sched, 0x09)->failures, 2);
    EXPECT_FALSE(find(sched, 0x09)->disabled);
    EXPECT_FALSE(cn105_test::TestLog::contains("disabled (not supported)"));

    // Third failure reaches maxFailures: the request is disabled for good.
    rec_.clear();
    sched.send_next_after(0x02);
    rec_.timeouts.back().fire();
    EXPECT_EQ(find(sched, 0x09)->failures, 3);
    EXPECT_TRUE(find(sched, 0x09)->disabled);
    EXPECT_TRUE(cn105_test::TestLog::contains("disabled (not supported)"));

    // From now on the chain skips it.
    rec_.clear();
    sched.send_next_after(0x02);
    EXPECT_EQ(rec_.sent, (Codes{0x04}));
    EXPECT_TRUE(rec_.timeouts.empty());
}

TEST_F(RequestScheduler, Characterize_SoftTimeoutIgnoredWhenResponseArrived) {
    auto sched = rec_.make();
    add(sched, 0x09, 3, 500);
    add(sched, 0x04);

    sched.send_next_after(0x00);
    ASSERT_EQ(rec_.timeouts.size(), 1u);
    sched.process_response(0x09);
    ASSERT_EQ(rec_.sent, (Codes{0x09, 0x04}));

    // The scheduler item is not cancelled by the response: it fires later and finds nothing awaited.
    rec_.timeouts[0].fire();
    EXPECT_EQ(find(sched, 0x09)->failures, 0);
    EXPECT_EQ(rec_.sent, (Codes{0x09, 0x04})) << "no extra request from the stale timeout";
}

TEST_F(RequestScheduler, Characterize_ResponseResetsFailureCount) {
    auto sched = rec_.make();
    add(sched, 0x09, 3, 500);

    sched.send_next_after(0x00);
    rec_.timeouts.back().fire();
    ASSERT_EQ(find(sched, 0x09)->failures, 1);

    sched.send_next_after(0x00);
    sched.process_response(0x09);
    EXPECT_EQ(find(sched, 0x09)->failures, 0);
    EXPECT_FALSE(find(sched, 0x09)->awaiting);
}

// ════════════════════════════════════════════════════════════════
// BUG-07 — no timeout without soft_timeout_ms, and late responses restart the chain
// ════════════════════════════════════════════════════════════════

// Pins current buggy behaviour — flip in Phase 5 (LINK-03)
// Mechanism (AUDIT BUG-07): a request with soft_timeout_ms == 0 (0x02, 0x03, 0x06, 0x04 in the registered
// table) schedules no timeout at all, so a response that never comes leaves the chain stuck on it.
TEST_F(RequestScheduler, Characterize_BUG07_ZeroSoftTimeoutSchedulesNothing) {
    auto sched = rec_.make();
    add(sched, 0x02, 3, 0);
    add(sched, 0x03, 3, 0);

    sched.send_next_after(0x00);

    EXPECT_EQ(rec_.sent, (Codes{0x02}));
    EXPECT_TRUE(rec_.timeouts.empty()) << "no timeout is scheduled";
    EXPECT_TRUE(find(sched, 0x02)->awaiting);

    // Time passing changes nothing: no further request, no cycle end.
    cn105_test::TestClock::advance(60000);
    EXPECT_EQ(rec_.sent, (Codes{0x02}));
    EXPECT_EQ(rec_.terminated, 0);
}

// Pins current buggy behaviour — flip in Phase 5 (LINK-04)
// Mechanism (AUDIT BUG-07): process_response() does not check that the code is the one awaited. A late or
// duplicate response for an earlier request marks it seen again and calls send_next_after(), which sends
// the request that is already in flight a second time.
TEST_F(RequestScheduler, Characterize_BUG07_LateResponseRestartsChain) {
    auto sched = rec_.make();
    add(sched, 0x02);
    add(sched, 0x03);
    add(sched, 0x06);

    sched.send_next_after(0x00);
    sched.process_response(0x02);
    ASSERT_EQ(rec_.sent, (Codes{0x02, 0x03}));
    ASSERT_TRUE(find(sched, 0x03)->awaiting);
    ASSERT_FALSE(find(sched, 0x02)->awaiting);

    // 0x02 is not awaited any more, yet its late response is processed and sends 0x03 once more.
    EXPECT_TRUE(sched.process_response(0x02));
    EXPECT_EQ(rec_.sent, (Codes{0x02, 0x03, 0x03}));
    EXPECT_EQ(rec_.terminated, 0);
}

// ════════════════════════════════════════════════════════════════
// RequestSchedulerComponent — the scheduler wired into the real CN105Climate
// ════════════════════════════════════════════════════════════════

class RequestSchedulerComponent : public cn105_test::Cn105Fixture {
   protected:
    /// Info codes (byte 5) of the FC 42 request frames written since the last call.
    Codes take_info_codes() {
        Codes codes;
        for (const auto& frame : take_tx_frames()) {
            if (frame.size() >= 6 && frame[1] == 0x42) codes.push_back(frame[5]);
        }
        return codes;
    }

    /// Steps in 10 ms ticks until an info request is written (returns its code) or max_ms elapsed (0xFF).
    uint8_t wait_for_request(uint32_t max_ms) {
        for (uint32_t elapsed = 0; elapsed < max_ms; elapsed += 10) {
            step(10);
            const Codes codes = take_info_codes();
            if (!codes.empty()) return codes.front();
        }
        return 0xFF;
    }
};

TEST_F(RequestSchedulerComponent, Characterize_RegisteredInfoRequestTable) {
    struct Expected {
        uint8_t code;
        uint8_t max_failures;
        uint32_t soft_timeout_ms;
        bool disabled;
    };
    // registerInfoRequests() + registerHardwareSettingsRequests(), no hardware setting selector configured:
    // the functions requests 0x20 / 0x22 are registered but disabled, the timers request 0x05 is a disabled
    // placeholder.
    const Expected expected[] = {
        {0x02, 3, 0, false},   {0x03, 3, 0, false}, {0x06, 3, 0, false}, {0x09, 3, 500, false},
        {0x42, 3, 500, false}, {0x04, 3, 0, false}, {0x05, 1, 0, true},  {0x20, 3, 0, true},
        {0x22, 3, 0, true},
    };

    const auto& requests = hp().scheduler_.requests_;
    ASSERT_EQ(requests.size(), sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < requests.size(); i++) {
        SCOPED_TRACE(i);
        EXPECT_EQ(requests[i].code, expected[i].code);
        EXPECT_EQ(requests[i].maxFailures, expected[i].max_failures);
        EXPECT_EQ(requests[i].soft_timeout_ms, expected[i].soft_timeout_ms);
        EXPECT_EQ(requests[i].disabled, expected[i].disabled);
        EXPECT_FALSE(requests[i].awaiting);
        EXPECT_EQ(requests[i].failures, 0);
    }
    EXPECT_EQ(requests[4].timeout_name, "info_timeout_0x42");
    EXPECT_FALSE(static_cast<bool>(requests[0].canSend));
    EXPECT_TRUE(static_cast<bool>(requests[4].canSend)) << "only the HVAC options request has a canSend condition";
}

TEST_F(RequestSchedulerComponent, Characterize_HvacOptionsSkippedWithoutSwitches) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();

    const Codes requested = run_poll_cycle();
    // 0x42 (HVAC options) is registered but canSend is false without an air purifier / night mode /
    // circulator switch, so it never goes on the wire.
    EXPECT_EQ(requested, (Codes{0x02, 0x03, 0x06, 0x09, 0x04}));
}

// Pins current buggy behaviour — flip in Phase 5 (LINK-03)
// Mechanism (AUDIT BUG-07): 0x02 has soft_timeout_ms == 0, so when its response never comes nothing
// advances the chain: the component only recovers when the whole cycle times out (2 * interval + 1000 ms
// after the start), then waits the update interval and starts a new cycle.
TEST_F(RequestSchedulerComponent, Characterize_BUG07_MissingResponseStallsUntilCycleTimeout) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();

    ASSERT_EQ(wait_for_request(5000), 0x02);
    const uint64_t cycle_start = cn105_test::TestClock::now64();
    ASSERT_TRUE(hp().loopCycle.isCycleRunning());

    // No response. Nothing more is written until the cycle timeout, strictly more than 3000 ms later.
    Codes during_stall;
    uint64_t timeout_at = 0;
    while (cn105_test::TestClock::now64() < cycle_start + 3000) {
        step(10);
        const Codes codes = take_info_codes();
        during_stall.insert(during_stall.end(), codes.begin(), codes.end());
    }
    EXPECT_TRUE(during_stall.empty()) << "the chain is stuck on 0x02";
    EXPECT_TRUE(hp().loopCycle.isCycleRunning());
    EXPECT_FALSE(cn105_test::TestLog::contains("Cycle timeout"));
    EXPECT_TRUE(hp().scheduler_.requests_[0].awaiting);

    for (uint32_t waited = 0; hp().loopCycle.isCycleRunning() && waited < 2000; waited += 10) step(10);
    timeout_at = cn105_test::TestClock::now64();
    EXPECT_FALSE(hp().loopCycle.isCycleRunning());
    EXPECT_TRUE(cn105_test::TestLog::contains("Cycle timeout"));
    EXPECT_GT(timeout_at - cycle_start, 3000u);
    EXPECT_LE(timeout_at - cycle_start, 3020u);

    // The next cycle starts more than one interval after the timeout, and 0x02 is still flagged awaiting
    // (nothing reset it); the new cycle simply requests 0x02 again.
    EXPECT_TRUE(hp().scheduler_.requests_[0].awaiting);
    ASSERT_EQ(wait_for_request(3000), 0x02);
    const uint64_t next_start = cn105_test::TestClock::now64();
    EXPECT_GT(next_start - timeout_at, 1000u);
    EXPECT_LE(next_start - timeout_at, 1030u);
}

// Pins current buggy behaviour — flip in Phase 5 (LINK-04)
// Mechanism (AUDIT BUG-07): while 0x06 is awaited, a late 0x03 response is handled as if it were the
// answer in the chain: send_next_after(0x03) sends 0x06 a second time, so the request is duplicated.
TEST_F(RequestSchedulerComponent, Characterize_BUG07_LateResponseDuplicatesChain) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();
    const auto responses = default_responses();

    // Answer 0x02 and 0x03 in order, leave 0x06 unanswered.
    ASSERT_EQ(wait_for_request(5000), 0x02);
    feed(responses.at(0x02));
    step(10);
    Codes requested = take_info_codes();
    ASSERT_EQ(requested, (Codes{0x03}));
    feed(responses.at(0x03));
    step(10);
    requested = take_info_codes();
    ASSERT_EQ(requested, (Codes{0x06})) << "0x06 is now awaited";

    // A late (duplicate) 0x03 response arrives while 0x06 is awaited.
    feed(responses.at(0x03));
    step(10);
    requested = take_info_codes();
    EXPECT_EQ(requested, (Codes{0x06})) << "0x06 requested a second time";

    // Answering 0x06 then moves on once; the duplicate request is not tracked separately.
    feed(responses.at(0x06));
    step(10);
    EXPECT_EQ(take_info_codes(), (Codes{0x09}));
}

// ════════════════════════════════════════════════════════════════
// HVAC options request, enabled by a switch (no real 0x42 capture exists, see SUMMARY)
// ════════════════════════════════════════════════════════════════

class RequestSchedulerHvacOptions : public RequestSchedulerComponent {
   protected:
    void before_setup(esphome::CN105Climate& hp) override { hp.set_air_purifier_switch(&air_purifier_); }
    esphome::HVACOptionSwitch air_purifier_;
};

TEST_F(RequestSchedulerHvacOptions, Characterize_HvacOptionsRequestedWithSwitchAndSoftTimeout) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();

    // default_responses() has no 0x42 answer: the request goes out, nothing answers, and the 500 ms soft
    // timeout lets the chain continue with 0x04.
    const Codes requested = run_poll_cycle(default_responses(), 5000);
    EXPECT_EQ(requested, (Codes{0x02, 0x03, 0x06, 0x09, 0x42, 0x04}));
    EXPECT_EQ(hp().scheduler_.requests_[4].failures, 1);
    EXPECT_TRUE(cn105_test::TestLog::contains("Soft timeout for"));
}
