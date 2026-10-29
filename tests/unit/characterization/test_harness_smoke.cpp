/// test_harness_smoke.cpp — proves the host harness: real CN105Climate on the real ESPHome 2026.10.0 core
/// Role: smoke tests of the harness itself (build flavour, fixture, test clock, platform control API).
/// Deps: fixture.h, test_platform.h, cn105_types.h (CONNECT), cn105_protocol.h (checksum)

#include "fixture.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "cn105_protocol.h"
#include "real_frames.h"
#include "esphome/components/wifi/wifi_component.h"
#ifdef USE_ESP32
#include "driver/gpio.h"
#include "driver/uart.h"
#endif

// ════════════════════════════════════════════════════════════════
// Build flavour
// ════════════════════════════════════════════════════════════════

// D-20: ILP32, so timestamp arithmetic behaves as on ESP32/ESP8266.
static_assert(sizeof(unsigned long) == 4, "the harness must be built with -m32");
// Fails if the pure-test stub version.h (ESPHOME_VERSION_CODE 2026.3.0) leaks onto the include path.
static_assert(ESPHOME_VERSION_CODE == VERSION_CODE(2026, 10, 0), "the real ESPHome 2026.10.0 headers must be used");

// ════════════════════════════════════════════════════════════════
// HarnessSmoke — the tracer: handshake on the test clock
// ════════════════════════════════════════════════════════════════

class HarnessSmoke : public cn105_test::Cn105Fixture {};

TEST_F(HarnessSmoke, ConnectFrameWrittenAfterGraceDelay) {
    // conn_bootstrap_delay_ms_ is 10000: nothing may be written before the grace delay elapsed.
    // Observed first-write window: loop at t=500 leaves BOOT, the first write happens at t=10000.
    for (int i = 1; i < 20; i++) {
        step(500);
        ASSERT_TRUE(uart_.tx.empty()) << "unexpected write at t=" << cn105_test::TestClock::now64() << " ms";
    }
    step(500);
    ASSERT_EQ(cn105_test::TestClock::now64(), 10000u);

    const std::vector<uint8_t> expected = {0xFC, 0x5A, 0x01, 0x30, 0x02, 0xCA, 0x01, 0xA8};
    EXPECT_EQ(take_tx(), expected);

    // The expected bytes are the CONNECT constant of cn105_types.h, checksum included.
    EXPECT_EQ(expected, std::vector<uint8_t>(CONNECT, CONNECT + CONNECT_LEN));
    EXPECT_EQ(cn105_protocol::checksum(expected.data(), static_cast<int>(expected.size()) - 1), expected.back());
}

// ════════════════════════════════════════════════════════════════
// HarnessSmoke — the full exchange: handshake, poll cycle, decode, command
// ════════════════════════════════════════════════════════════════

TEST_F(HarnessSmoke, HandshakeReplyConnects) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_TRUE(hp().isHeatpumpConnected());
    EXPECT_TRUE(cn105_test::TestLog::contains("CONNECTED"));
}

TEST_F(HarnessSmoke, FirstPollCycleRequestsInfoCodesInOrder) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();

    const std::vector<uint8_t> requested = run_poll_cycle();
    const std::vector<uint8_t> expected_codes = {0x02, 0x03, 0x06, 0x09, 0x04};
    EXPECT_EQ(requested, expected_codes);
}

TEST_F(HarnessSmoke, PollCycleRequestFramesCarryTheirChecksums) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();

    // Record the raw request frames of one cycle by answering with default_responses().
    const auto responses = default_responses();
    std::vector<std::vector<uint8_t>> requests;
    bool started = false;
    for (uint32_t elapsed = 0; elapsed < 5000; elapsed += 10) {
        step(10);
        for (const auto& frame : take_tx_frames()) {
            if (frame.size() < 6 || frame[1] != 0x42) continue;
            requests.push_back(frame);
            auto it = responses.find(frame[5]);
            if (it != responses.end()) feed(it->second);
        }
        if (hp().loopCycle.isCycleRunning()) started = true;
        if (started && !hp().loopCycle.isCycleRunning()) break;
    }
    ASSERT_EQ(requests.size(), 5u);
    const uint8_t expected_checksums[] = {0x7B, 0x7A, 0x77, 0x74, 0x79};
    for (size_t i = 0; i < requests.size(); i++) {
        EXPECT_EQ(requests[i].size(), 22u) << cn105_test::hex(requests[i]);
        EXPECT_EQ(requests[i].back(), expected_checksums[i]) << cn105_test::hex(requests[i]);
    }
}

TEST_F(HarnessSmoke, PollCyclePublishesDecodedState) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    take_tx();
    publishes_.clear();

    run_poll_cycle();
    ASSERT_FALSE(publishes_.empty());
    const cn105_test::Published& last = publishes_.back();
    EXPECT_EQ(last.mode, esphome::climate::CLIMATE_MODE_HEAT);
    EXPECT_FLOAT_EQ(last.target_temperature, 21.5f);
    EXPECT_FLOAT_EQ(last.current_temperature, 23.0f);
    // Observed with the unmodified component: STATUS_IDLE gives IDLE, SETTINGS fan byte 0 gives AUTO,
    // vane byte 0 leaves swing OFF. One publish per decoded response (settings, room temperature,
    // status, power): four in this cycle; the first has no current temperature yet.
    EXPECT_EQ(last.action, esphome::climate::CLIMATE_ACTION_IDLE);
    ASSERT_TRUE(last.fan_mode.has_value());
    EXPECT_EQ(*last.fan_mode, esphome::climate::CLIMATE_FAN_AUTO);
    EXPECT_EQ(last.swing_mode, esphome::climate::CLIMATE_SWING_OFF);
    EXPECT_EQ(publishes_.size(), 4u);
    EXPECT_TRUE(std::isnan(publishes_.front().current_temperature));
}

TEST_F(HarnessSmoke, ClimateCallProducesSetFrame) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle();
    take_tx();
    publishes_.clear();

    hp().make_call().set_mode(esphome::climate::CLIMATE_MODE_HEAT).set_target_temperature(23.0f).perform();
    run_for(2000);

    const auto frames = take_tx_frames();
    std::vector<std::vector<uint8_t>> set_frames;
    for (const auto& f : frames) {
        if (f.size() > 1 && f[1] == 0x41) set_frames.push_back(f);
    }
    ASSERT_EQ(set_frames.size(), 1u);
    const std::vector<uint8_t> expected = {0xFC, 0x41, 0x01, 0x30, 0x10, 0x01, 0x07, 0x00, 0x01, 0x01, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAE, 0x00, 0xC6};
    EXPECT_EQ(set_frames[0], expected) << cn105_test::hex(set_frames[0]);
    EXPECT_EQ(publishes_.size(), 2u);
}

// ════════════════════════════════════════════════════════════════
// HarnessClock — time only moves when a test moves it (D-05)
// ════════════════════════════════════════════════════════════════

class HarnessClock : public cn105_test::ClockFixture {};

TEST_F(HarnessClock, MillisWrapsAt32Bits) {
    cn105_test::TestClock::set(0xFFFFFFF0ull);
    cn105_test::TestClock::advance(0x20);
    EXPECT_EQ(esphome::millis(), 0x10u);
    EXPECT_EQ(cn105_test::TestClock::now64(), 0x100000010ull);
    EXPECT_EQ(cn105_test::TestClock::now(), 0x10u);
}

TEST_F(HarnessClock, DelayAdvancesWithoutSleeping) {
    const uint64_t before = cn105_test::TestClock::now64();
    const auto wall_start = std::chrono::steady_clock::now();
    esphome::delay(5000);
    const auto wall_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - wall_start).count();
    EXPECT_EQ(cn105_test::TestClock::now64() - before, 5000u);
    EXPECT_LT(wall_ms, 1000);
}

TEST_F(HarnessClock, Millis64ContinuesAcrossWrap) {
    // ESPHome's 64-bit time (the scheduler's time base) follows the same TestClock across the 2^32
    // wrap. Assert the delta, not the absolute value, so rollover state left by an earlier test in the
    // same process cannot change the result.
    cn105_test::TestClock::set(0xFFFFFFF0ull);
    const uint64_t m0 = esphome::millis_64();
    cn105_test::TestClock::advance(0x20);
    const uint64_t m1 = esphome::millis_64();
    EXPECT_EQ(m1 - m0, 0x20u);
    EXPECT_EQ(static_cast<uint32_t>(m1), 0x10u);
}

// ════════════════════════════════════════════════════════════════
// HarnessPlatform — logs, preferences, Wi-Fi and IDF calls are observable
// ════════════════════════════════════════════════════════════════

class HarnessPlatform : public cn105_test::ClockFixture {};

TEST_F(HarnessPlatform, LogCaptureKeepsLevelTagAndMessage) {
    ESP_LOGW("harness_tag", "answer=%d name=%s", 42, "alpha");
    ESP_LOGW("harness_tag", "answer=%d name=%s", 43, "alpha");

    const auto& records = cn105_test::TestLog::records();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].level, ESPHOME_LOG_LEVEL_WARN);
    EXPECT_EQ(records[0].tag, "harness_tag");
    EXPECT_EQ(records[0].message, "answer=42 name=alpha");
    EXPECT_TRUE(cn105_test::TestLog::contains("answer=43"));
    EXPECT_FALSE(cn105_test::TestLog::contains("answer=44"));
    EXPECT_EQ(cn105_test::TestLog::count("name=alpha"), 2u);
    EXPECT_EQ(cn105_test::TestLog::count("answer=42"), 1u);

    cn105_test::TestLog::clear();
    EXPECT_TRUE(cn105_test::TestLog::records().empty());
    EXPECT_FALSE(cn105_test::TestLog::contains("answer=42"));
}

TEST_F(HarnessPlatform, PrefsRoundTripInMemory) {
    struct Blob {
        uint32_t a;
        uint8_t b;
    };
    EXPECT_EQ(cn105_test::TestPrefs::size(), 0u);

    auto pref = esphome::global_preferences->make_preference<Blob>(0x1234u);
    const Blob in{7u, 9u};
    EXPECT_TRUE(pref.save(&in));
    EXPECT_EQ(cn105_test::TestPrefs::size(), 1u);

    Blob out{};
    EXPECT_TRUE(pref.load(&out));
    EXPECT_EQ(out.a, 7u);
    EXPECT_EQ(out.b, 9u);

    cn105_test::TestPrefs::clear();
    EXPECT_EQ(cn105_test::TestPrefs::size(), 0u);
    EXPECT_FALSE(pref.load(&out));
}

TEST_F(HarnessPlatform, WifiStubControlsGlobal) {
    EXPECT_EQ(esphome::wifi::global_wifi_component, nullptr);

    cn105_test::TestWifi::install(true);
    ASSERT_NE(esphome::wifi::global_wifi_component, nullptr);
    EXPECT_TRUE(esphome::wifi::global_wifi_component->is_connected());

    cn105_test::TestWifi::set_connected(false);
    EXPECT_FALSE(esphome::wifi::global_wifi_component->is_connected());

    cn105_test::TestWifi::remove();
    EXPECT_EQ(esphome::wifi::global_wifi_component, nullptr);
}

#ifdef USE_ESP32
// ESP32 only: the ESP8266 build has no IDF layer.
TEST_F(HarnessPlatform, IdfCallsRecorded) {
    EXPECT_TRUE(cn105_test::IdfRecorder::calls().empty());

    uart_set_pin(1, 4, 5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    gpio_reset_pin(7);

    const auto& calls = cn105_test::IdfRecorder::calls();
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[0].fn, "uart_set_pin");
    EXPECT_EQ(calls[0].a0, 1);
    EXPECT_EQ(calls[0].a1, 4);
    EXPECT_EQ(calls[0].a2, 5);
    EXPECT_EQ(calls[0].a3, UART_PIN_NO_CHANGE);
    EXPECT_EQ(calls[1].fn, "gpio_reset_pin");
    EXPECT_EQ(calls[1].a0, 7);

    cn105_test::IdfRecorder::clear();
    EXPECT_TRUE(cn105_test::IdfRecorder::calls().empty());
}
#endif

// ════════════════════════════════════════════════════════════════
// HarnessIsolation — every test starts from reset platform state
// ════════════════════════════════════════════════════════════════
// Order-dependent by design: StateIsResetInSetUp must run right after Pollute in the same process.
// gtest runs the tests of a suite in definition order; the binaries are run without --gtest_shuffle.

class HarnessIsolation : public cn105_test::Cn105Fixture {
   protected:
    cn105_test::Cn105Options options() const override {
        cn105_test::Cn105Options opts;
        opts.start_ms = 5000;
        return opts;
    }
};

TEST_F(HarnessIsolation, Pollute) {
    EXPECT_EQ(cn105_test::TestClock::now64(), 5000u);
    cn105_test::TestClock::set(0x12345678ull);
    ESP_LOGW("harness_tag", "POLLUTION-MARKER");
    struct Blob {
        uint32_t a;
    };
    auto pref = esphome::global_preferences->make_preference<Blob>(0x4321u);
    const Blob in{1u};
    pref.save(&in);
    cn105_test::TestWifi::install(true);
#ifdef USE_ESP32
    gpio_reset_pin(3);
#endif
}

TEST_F(HarnessIsolation, StateIsResetInSetUp) {
    EXPECT_EQ(cn105_test::TestClock::now64(), 5000u);
    EXPECT_FALSE(cn105_test::TestLog::contains("POLLUTION-MARKER"));
    EXPECT_EQ(cn105_test::TestPrefs::size(), 0u);
    EXPECT_EQ(esphome::wifi::global_wifi_component, nullptr);
#ifdef USE_ESP32
    EXPECT_TRUE(cn105_test::IdfRecorder::calls().empty());
#endif
}
