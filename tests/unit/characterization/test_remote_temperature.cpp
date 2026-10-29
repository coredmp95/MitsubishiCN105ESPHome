/// test_remote_temperature.cpp — Characterization of the remote-temperature path
/// Role: pins, on the unmodified component, how set_remote_temperature() travels to the UART (0x41/0x07
///       packet written at the end of the next poll cycle), compared byte for byte with a real capture,
///       and (PR 1b, Task 3 of plan 01-12) the keep-alive, debounce, timeout fallback, source-info text,
///       profile acceptance and the BUG-02 margin quirk, before Phase 4 (TIME-04) changes them.
/// Deps: fixture.h, real_frames.h, cn105.h

#include "fixture.h"

#include <cmath>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "captured_frames_1b.h"  // empty until the maintainer supplies captures (see README: Wanted captures)
#include "real_frames.h"

namespace {

using cn105_test::hex;

/// True for a 0x41 SET frame carrying the remote-temperature flag (info code byte 5 = 0x07).
bool is_remote_temp_frame(const std::vector<uint8_t>& f) { return f.size() > 5 && f[1] == 0x41 && f[5] == 0x07; }

}  // namespace

class RemoteTemperature : public cn105_test::Cn105Fixture {
   protected:
    /// Steps `total_ms` of test time with a 10 ms tick, answering every FC 42 request with the default real
    /// responses, and returns every frame the component wrote meanwhile (checksum included).
    std::vector<std::vector<uint8_t>> poll_and_collect(uint32_t total_ms) {
        std::vector<std::vector<uint8_t>> all;
        const auto responses = default_responses();
        for (uint32_t elapsed = 0; elapsed < total_ms; elapsed += 10) {
            step(10);
            for (const auto& frame : take_tx_frames()) {
                all.push_back(frame);
                if (frame.size() > 5 && frame[1] == 0x42) {
                    auto it = responses.find(frame[5]);
                    if (it != responses.end()) feed(it->second);
                }
            }
        }
        return all;
    }
};

// ════════════════════════════════════════════════════════════════
// Remote-temperature packet vs the real capture
// ════════════════════════════════════════════════════════════════

TEST_F(RemoteTemperature, Characterize_RemoteTempPacketMatchesRealCapture) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle();
    take_tx();

    hp().set_remote_temperature(22.8f);

    // The packet is not written by set_remote_temperature() itself: it leaves at the end of the next
    // poll cycle (terminateCycle -> sendRemoteTemperature).
    const auto frames = poll_and_collect(3000);
    std::vector<std::vector<uint8_t>> remote_frames;
    for (const auto& f : frames) {
        if (is_remote_temp_frame(f)) remote_frames.push_back(f);
    }
    ASSERT_EQ(remote_frames.size(), 1u) << "exactly one remote temperature packet expected";
    EXPECT_EQ(remote_frames[0], cn105_test::with_checksum(cn105_test::real_frames::SET_REMOTE_TEMP_22_8))
        << "written: " << hex(remote_frames[0]);
}

// ════════════════════════════════════════════════════════════════
// Catalog with the optional entities attached (PR 1b)
// ════════════════════════════════════════════════════════════════

/// RemoteTemperature plus what climate.py to_code attaches for the remote-temperature options: the
/// "remote temperature in use" binary sensor with its margin, the timeout and the keep-alive interval.
/// Every production setup sets the timeout explicitly, so the fixture does too (the zero-filled storage
/// would otherwise leave remote_temp_timeout_ at 0 ms, audit BUG-08).
class RemoteTemperatureEntities : public RemoteTemperature {
   protected:
    virtual uint32_t timeout_ms() const { return 600000; }
    virtual uint32_t keepalive_ms() const { return 20000; }

    void before_setup(esphome::CN105Climate& hp) override {
        hp.set_remote_temperature_control_sensor(&remote_in_use_);
        hp.set_remote_temperature_margin(0.4f);
        hp.set_remote_temp_keepalive_interval(keepalive_ms());
        hp.set_remote_temp_timeout(timeout_ms());
    }

    /// Steps `total_ms` like poll_and_collect and returns the test-clock time of every remote-temperature
    /// frame together with the frame itself.
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> remote_frames_over(uint32_t total_ms) {
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> out;
        const auto responses = default_responses();
        for (uint32_t elapsed = 0; elapsed < total_ms; elapsed += 10) {
            step(10);
            for (const auto& frame : take_tx_frames()) {
                if (is_remote_temp_frame(frame)) out.emplace_back(esphome::millis(), frame);
                if (frame.size() > 5 && frame[1] == 0x42) {
                    auto it = responses.find(frame[5]);
                    if (it != responses.end()) feed(it->second);
                }
            }
        }
        return out;
    }

    /// Connects, completes one poll cycle and drops what was written.
    void connected_and_idle() {
        connect();
        ASSERT_FALSE(HasFatalFailure());
        run_poll_cycle();
        take_tx();
    }

    esphome::binary_sensor::BinarySensor remote_in_use_;
};

// ════ Keep-alive and debounce ═════════════════════════════════

TEST_F(RemoteTemperatureEntities, Characterize_KeepAliveResendsAfterInterval) {
    connected_and_idle();
    ASSERT_FALSE(HasFatalFailure());

    hp().set_remote_temperature(22.8f);
    const auto frames = remote_frames_over(50000);

    // The value is sent once when it arrives, then re-sent by the keep-alive without any new
    // set_remote_temperature(): the keep-alive only raises shouldSendExternalTemperature_, the packet leaves
    // at the end of the next poll cycle. ESPHome staggers a new interval by a random offset in [0, 5 s) before
    // its first tick (the test platform makes it deterministic but it differs per variant), so only the
    // spacing is pinned, not the absolute time. A first tick within 10 s of the initial send is swallowed by
    // the debounce (min interval = max(keep-alive / 2, 5 s) = 10 s); later ticks are 20 s apart.
    ASSERT_EQ(frames.size(), 3u);
    const auto expected = cn105_test::with_checksum(cn105_test::real_frames::SET_REMOTE_TEMP_22_8);
    for (const auto& f : frames) EXPECT_EQ(f.second, expected) << hex(f.second);
    EXPECT_GE(frames[1].first - frames[0].first, 10000u);
    EXPECT_LT(frames[1].first - frames[0].first, 26000u);
    EXPECT_GE(frames[2].first - frames[1].first, 18000u);
    EXPECT_LT(frames[2].first - frames[1].first, 23000u);
    EXPECT_TRUE(hp().remote_temp_keepalive_active_);
}

TEST_F(RemoteTemperatureEntities, Characterize_SameValueDebounced) {
    connected_and_idle();
    ASSERT_FALSE(HasFatalFailure());

    hp().set_remote_temperature(22.8f);
    ASSERT_EQ(remote_frames_over(3000).size(), 1u);

    // Same value again 3 s later: still inside the debounce window (max(keep-alive / 2, 5 s) = 10 s).
    // The watchdog is refreshed, the packet is not rewritten.
    cn105_test::TestLog::clear();
    hp().set_remote_temperature(22.8f);
    EXPECT_TRUE(hp().shouldSendExternalTemperature_);
    EXPECT_TRUE(remote_frames_over(3000).empty()) << "identical value inside the debounce window";
    EXPECT_TRUE(cn105_test::TestLog::contains("Debounce: skipping remote temp send"));
    EXPECT_EQ(hp().remote_temp_debounce_skip_count_, 1);
    EXPECT_FALSE(hp().shouldSendExternalTemperature_) << "the flag is consumed even when the send is skipped";

    // A changed value is never debounced.
    hp().set_remote_temperature(23.1f);
    const auto changed = remote_frames_over(3000);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(hp().remote_temp_debounce_skip_count_, 0);
    EXPECT_EQ(hp().last_remote_temp_sent_, 23.1f);
}

// ════ Timeout fallback ════════════════════════════════════════

class RemoteTemperatureShortTimeout : public RemoteTemperatureEntities {
   protected:
    uint32_t timeout_ms() const override { return 5000; }
};

TEST_F(RemoteTemperatureShortTimeout, Characterize_TimeoutFallsBackToInternal) {
    connected_and_idle();
    ASSERT_FALSE(HasFatalFailure());

    hp().set_remote_temperature(22.8f);
    cn105_test::TestLog::clear();

    // No further value arrives: 5 s after the last accepted sample the watchdog calls
    // set_remote_temperature(0). That stops the keep-alive and queues one last packet carrying 0, which is
    // how the unit is told to use its own sensor again.
    const auto frames = remote_frames_over(9000);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].second, cn105_test::with_checksum(cn105_test::real_frames::SET_REMOTE_TEMP_22_8));

    // Fallback frame: the same 0x41 / 0x07 packet with the remote-temperature enable flag cleared. Observed.
    const std::vector<uint8_t>& fallback = frames[1].second;
    EXPECT_EQ(fallback[1], 0x41);
    EXPECT_EQ(fallback[5], 0x07);
    EXPECT_EQ(fallback[6], 0x00) << "remote-temperature enable byte cleared: " << hex(fallback);
    EXPECT_GE(frames[1].first - frames[0].first, 3000u);
    EXPECT_TRUE(cn105_test::TestLog::contains("Remote temperature timeout occured, fall back to internal temperature!"));
    EXPECT_EQ(hp().remoteTemperature_, 0.0f);
    EXPECT_FALSE(hp().remote_temp_keepalive_active_);

    // The "remote in use" binary sensor follows on the next room-temperature reply.
    EXPECT_FALSE(remote_in_use_.state);
}

// ════ Source info text sensor ═════════════════════════════════

/// remote_temperature source: a sensor whose published value feeds set_remote_temperature(), and a text
/// sensor announcing which sensor that is. The order of the two setters matters (see the reversed suite).
class RemoteTemperatureSource : public RemoteTemperatureEntities {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        RemoteTemperatureEntities::before_setup(hp);
        source_.configure_entity_("Hallway thermometer", 0, 0);
        hp.set_remote_temp_source(&source_);
        hp.set_remote_temp_source_info_sensor(&info_);
    }
    esphome::sensor::Sensor source_;
    esphome::RemoteTempSourceInfo info_;
};

TEST_F(RemoteTemperatureSource, Characterize_SourceInfoTextSensor) {
    // The info sensor is published once, at setup, with the NAME of the source sensor; nothing else ever
    // writes to it (neither a source value nor the timeout fallback changes the text).
    EXPECT_EQ(info_.state, "Hallway thermometer");

    connected_and_idle();
    ASSERT_FALSE(HasFatalFailure());

    // A source reading is forwarded as the remote temperature.
    source_.publish_state(22.8f);
    EXPECT_EQ(hp().remoteTemperature_, 22.8f);
    const auto frames = remote_frames_over(3000);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].second, cn105_test::with_checksum(cn105_test::real_frames::SET_REMOTE_TEMP_22_8));

    // An invalid reading is dropped by the profile, the previous value stays.
    source_.publish_state(NAN);
    EXPECT_EQ(hp().remoteTemperature_, 22.8f);
    EXPECT_EQ(info_.state, "Hallway thermometer");
}

class RemoteTemperatureSourceReversed : public RemoteTemperatureEntities {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        RemoteTemperatureEntities::before_setup(hp);
        source_.configure_entity_("Hallway thermometer", 0, 0);
        hp.set_remote_temp_source_info_sensor(&info_);  // before the source is known
        hp.set_remote_temp_source(&source_);
    }
    esphome::sensor::Sensor source_;
    esphome::RemoteTempSourceInfo info_;
};

TEST_F(RemoteTemperatureSourceReversed, Characterize_SourceInfoNotPublishedWhenSetBeforeSource) {
    // set_remote_temp_source_info_sensor() publishes only if the source is already attached: with the other
    // order the text stays empty. climate.py attaches the source first, so YAML users are not affected.
    EXPECT_EQ(info_.state, "");
    EXPECT_FALSE(info_.has_state());
}

// ════ Profile acceptance ══════════════════════════════════════

TEST_F(RemoteTemperatureEntities, Characterize_ProfileAcceptsRemoteTemperatureRange) {
    // HeatPumpProfile::accepts_remote_temperature rejects only NaN: there is no range check.
    hp().set_remote_temperature(NAN);
    EXPECT_EQ(hp().remoteTemperature_, 0.0f);
    EXPECT_FALSE(hp().shouldSendExternalTemperature_);
    EXPECT_FALSE(hp().remote_temp_keepalive_active_);
    EXPECT_TRUE(cn105_test::TestLog::contains("Remote temperature is NaN, ignoring."));

    // 0 is accepted: it is the "use the internal sensor" request. No keep-alive.
    hp().set_remote_temperature(0.0f);
    EXPECT_TRUE(hp().shouldSendExternalTemperature_);
    EXPECT_FALSE(hp().remote_temp_keepalive_active_);

    // A negative value is accepted and treated like 0 for the keep-alive (no keep-alive), but stored as is.
    hp().shouldSendExternalTemperature_ = false;
    hp().set_remote_temperature(-5.0f);
    EXPECT_EQ(hp().remoteTemperature_, -5.0f);
    EXPECT_TRUE(hp().shouldSendExternalTemperature_);
    EXPECT_FALSE(hp().remote_temp_keepalive_active_);

    // An absurd high value is accepted too and starts the keep-alive.
    hp().set_remote_temperature(100.0f);
    EXPECT_EQ(hp().remoteTemperature_, 100.0f);
    EXPECT_TRUE(hp().remote_temp_keepalive_active_);
}

// ════ BUG-02 ══════════════════════════════════════════════════

// Pins current buggy behaviour — flip in Phase 4 (TIME-04): getRoomTemperatureFromResponsePacket() computes
// the gap as abs(float - float) in a translation unit where abs() resolves to the integer overload, so the
// gap is truncated toward zero. With a 0.4 C margin, a 0.6 C gap becomes 0 and passes as "remote in use".
// Expected after the fix: false for 0.6 C (0.6 > 0.4).
TEST_F(RemoteTemperatureEntities, Characterize_BUG02_MarginUsesIntegerAbs) {
    connected_and_idle();
    ASSERT_FALSE(HasFatalFailure());

    // ROOM_TEMP_23C decodes to 23.0 C. Remote 22.4 C: gap 0.6 C > margin 0.4 C.
    hp().set_remote_temperature(22.4f);
    ASSERT_TRUE(hp().remote_temp_keepalive_active_);
    run_poll_cycle();
    EXPECT_FLOAT_EQ(hp().currentStatus.roomTemperature, 23.0f);
    EXPECT_TRUE(remote_in_use_.state) << "BUG-02: the 0.6 C gap is truncated to 0 and reported as remote";
}

TEST_F(RemoteTemperatureEntities, Characterize_BUG02_GapAboveOneDegreeIsDetected) {
    connected_and_idle();
    ASSERT_FALSE(HasFatalFailure());

    // Remote 21.8 C: gap 1.2 C, truncated to 1, which is above the 0.4 C margin: the bug is invisible here.
    hp().set_remote_temperature(21.8f);
    run_poll_cycle();
    EXPECT_FLOAT_EQ(hp().currentStatus.roomTemperature, 23.0f);
    EXPECT_FALSE(remote_in_use_.state);

    // And an exact match is of course remote.
    hp().set_remote_temperature(23.0f);
    run_poll_cycle();
    EXPECT_TRUE(remote_in_use_.state);
}

// ════ Lossnay ═════════════════════════════════════════════════

class RemoteTemperatureLossnay : public cn105_test::Cn105Fixture {
   protected:
    cn105_test::Cn105Options options() const override {
        cn105_test::Cn105Options o;
        o.lossnay = true;
        return o;
    }
};

TEST_F(RemoteTemperatureLossnay, Characterize_LossnayRefusesRemoteTemperature) {
    // LossnayProfile::allow_operation() refuses every operation but logs a warning; the base
    // accepts_remote_temperature() relies on it, so the value is dropped before anything is stored.
    hp().set_remote_temperature(22.8f);
    EXPECT_EQ(hp().remoteTemperature_, 0.0f);
    EXPECT_FALSE(hp().shouldSendExternalTemperature_);
    EXPECT_FALSE(hp().remote_temp_keepalive_active_);
    EXPECT_TRUE(cn105_test::TestLog::contains("Ignoring unsupported Lossnay operation"));

    // sendRemoteTemperaturePacket() has its own guard: nothing is written even when called directly.
    take_tx();
    hp().sendRemoteTemperaturePacket();
    EXPECT_TRUE(take_tx().empty());
}
