/// test_climate_controls.cpp — Characterization of the climate command path (climateControls.cpp)
/// Role: pins, on the unmodified component, how a ClimateCall travels control() -> wantedSettings ->
///       immediate HA publish -> debounce/throttle gate -> 0x41 SET frame -> second publish -> reset,
///       the mode/fan/swing mappings, and both platform locking paths of control() (std::lock_guard on
///       ESP32, emulateMutex on ESP8266). Known bugs are pinned as they are (audit BUG-03, BUG-08)
///       before Phases 4 (TIME-03), 6 (CMD-03) and 7 (CLEAN-01) change them.
/// Deps: fixture.h, real_frames.h, cn105_protocol.h, cn105.h

#include "fixture.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "cn105_protocol.h"
#include "real_frames.h"

namespace {

using cn105_test::hex;
using namespace esphome::climate;

/// "<null>" for a nullptr C string, so a failed assertion prints something readable.
std::string str(const char* s) { return s == nullptr ? std::string("<null>") : std::string(s); }

/// A 0x41 SET frame as the component writes it: FC 41 01 30 10 01 + zeroed data, with the given
/// (index, value) bytes overridden and the checksum appended. Index 6/7 are the "what is set" flags,
/// 8 power, 9 mode, 10 temperature (table encoding), 11 fan, 12 vane, 18 wide vane, 19 temperature (encoding B).
std::vector<uint8_t> set_frame(std::initializer_list<std::pair<int, uint8_t>> bytes) {
    std::vector<uint8_t> f(21, 0x00);
    f[0] = 0xFC;
    f[1] = 0x41;
    f[2] = 0x01;
    f[3] = 0x30;
    f[4] = 0x10;
    f[5] = 0x01;
    for (const auto& b : bytes) f[static_cast<size_t>(b.first)] = b.second;
    return cn105_test::with_checksum(f);
}

/// Number of 0x41 frames in `frames`.
size_t count_set_frames_in(const std::vector<std::vector<uint8_t>>& frames) {
    size_t n = 0;
    for (const auto& f : frames) {
        if (f.size() > 1 && f[1] == 0x41) n++;
    }
    return n;
}

}  // namespace

class ClimateControls : public cn105_test::Cn105Fixture {
   protected:
    /// Connects and runs one poll cycle (the heat pump answers with `settings`, by default a real
    /// "ON, HEAT, 21.5" state), then clears what was recorded so far: the next publishes / bytes are those
    /// of the command under test.
    void connect_and_poll(const std::vector<uint8_t>& settings = cn105_test::real_frames::SETTINGS_ON_HEAT_21_5) {
        responses_ = default_responses();
        responses_[0x02] = settings;
        connect();
        if (HasFatalFailure()) return;
        run_poll_cycle(responses_);
        publishes_.clear();
        take_tx();
    }

    /// Steps by 10 ms until a 0x41 frame is written (max `max_ms`); returns it (empty when none) and
    /// leaves the time it was written at in `sent_at_ms_`. Other frames written meanwhile are dropped.
    std::vector<uint8_t> step_until_set_frame(uint32_t max_ms = 5000) {
        for (uint32_t waited = 0; waited < max_ms; waited += 10) {
            step(10);
            for (auto& f : take_tx_frames()) {
                if (f.size() > 1 && f[1] == 0x41) {
                    sent_at_ms_ = cn105_test::TestClock::now();
                    return f;
                }
            }
        }
        return {};
    }

    /// Performs `act` (a ClimateCall), lets the component send it and returns the 0x41 frame; then runs
    /// one poll cycle so the heat pump answers again and the next command starts from a settled state.
    std::vector<uint8_t> command_frame(const std::function<void()>& act) {
        act();
        auto frame = step_until_set_frame();
        run_poll_cycle(responses_);
        return frame;
    }

    std::map<uint8_t, std::vector<uint8_t>> responses_;
    uint32_t sent_at_ms_ = 0;
};

// ════════════════════════════════════════════════════════════════
// Command path end to end (tracer)
// ════════════════════════════════════════════════════════════════

// Pins current buggy behaviour — flip in Phase 6 (CMD-03)
// BUG-03: control() publishes the REQUESTED state to Home Assistant at once, before a single byte of the
// command reached the heat pump (finalizeControlIfUpdated -> publish_state()). If the heat pump never
// receives or applies the command, HA keeps showing a state the unit is not in.
TEST_F(ClimateControls, Characterize_BUG03_OptimisticPublishBeforeSend) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(publishes_.empty());
    const unsigned long last_send_before = hp().lastSend;

    const uint32_t t_command = cn105_test::TestClock::now();
    hp().make_call().set_mode(CLIMATE_MODE_HEAT).set_target_temperature(23.0f).perform();

    // --- no time has passed: HA already shows the requested state ---------------------------------
    ASSERT_EQ(publishes_.size(), 1u) << "control() publishes once, synchronously";
    EXPECT_EQ(publishes_[0].at_ms, t_command);
    EXPECT_EQ(publishes_[0].mode, CLIMATE_MODE_HEAT);
    EXPECT_FLOAT_EQ(publishes_[0].target_temperature, 23.0f);

    // --- nothing reached the heat pump ----------------------------------------------------------
    EXPECT_TRUE(uart_.tx.empty()) << "no byte written at control() time: " << hex(uart_.tx);

    // --- the wanted state holds the command, flagged as not yet sent -------------------------------
    EXPECT_EQ(str(hp().wantedSettings.mode), "HEAT");
    EXPECT_EQ(str(hp().wantedSettings.power), "ON");
    EXPECT_FLOAT_EQ(hp().wantedSettings.temperature, 23.0f);
    EXPECT_EQ(str(hp().wantedSettings.fan), "<null>");
    EXPECT_EQ(str(hp().wantedSettings.vane), "<null>");
    EXPECT_EQ(str(hp().wantedSettings.wideVane), "<null>");
    EXPECT_TRUE(hp().wantedSettings.hasChanged);
    EXPECT_FALSE(hp().wantedSettings.hasBeenSent);
    EXPECT_EQ(hp().wantedSettings.lastChange, static_cast<long>(t_command));

    // --- later: the SET frame leaves, gated by the 300 ms write throttle of the previous request ----
    const auto frame = step_until_set_frame();
    const auto expected = set_frame({{6, 0x07}, {8, 0x01}, {9, 0x01}, {19, 0xAE}});  // power+mode+temp; 23.0 -> 23*2+128
    EXPECT_EQ(frame, expected) << hex(frame);
    // debounce_delay_ is 0 here, so the only gate is `now - lastSend > 300` (strict): the previous info
    // request was written 10 ms before the command, hence the first 10 ms tick that satisfies it.
    EXPECT_EQ(sent_at_ms_, static_cast<uint32_t>(last_send_before) + 310);
    EXPECT_EQ(hp().lastSend, static_cast<unsigned long>(sent_at_ms_));

    // --- the second publish happens in the same call that writes the frame -------------------------
    ASSERT_EQ(publishes_.size(), 2u) << "one publish at control(), one at send time";
    EXPECT_EQ(publishes_[1].at_ms, sent_at_ms_);
    EXPECT_EQ(publishes_[1].mode, CLIMATE_MODE_HEAT);
    EXPECT_FLOAT_EQ(publishes_[1].target_temperature, 23.0f);

    // --- and the wanted state is reset (lastChange is the only field left behind) --------------------
    EXPECT_EQ(str(hp().wantedSettings.mode), "<null>");
    EXPECT_EQ(str(hp().wantedSettings.power), "<null>");
    EXPECT_FLOAT_EQ(hp().wantedSettings.temperature, -1.0f);
    EXPECT_FALSE(hp().wantedSettings.hasChanged);
    EXPECT_FALSE(hp().wantedSettings.hasBeenSent);
    EXPECT_EQ(hp().wantedSettings.lastChange, static_cast<long>(t_command));
    // The current (confirmed) state is untouched by the SET: nothing confirms the command (CMD-03/04).
    EXPECT_FLOAT_EQ(hp().currentSettings.temperature, 21.5f);
}

// ════════════════════════════════════════════════════════════════
// Mode, temperature, fan and swing mappings
// ════════════════════════════════════════════════════════════════

TEST_F(ClimateControls, Characterize_ModeMapping) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());

    // mode -> wanted mode string, wanted power string, SET frame (flags: 0x01 power, 0x02 mode).
    // OFF changes the power only; every other mode switches the power ON; FAN_ONLY is the hardware "FAN".
    const struct {
        ClimateMode mode;
        const char* label;
        const char* wanted_mode;
        const char* wanted_power;
        std::vector<uint8_t> frame;
    } cases[] = {
        {CLIMATE_MODE_OFF, "OFF", "<null>", "OFF", set_frame({{6, 0x01}})},
        {CLIMATE_MODE_HEAT, "HEAT", "HEAT", "ON", set_frame({{6, 0x03}, {8, 0x01}, {9, 0x01}})},
        {CLIMATE_MODE_DRY, "DRY", "DRY", "ON", set_frame({{6, 0x03}, {8, 0x01}, {9, 0x02}})},
        {CLIMATE_MODE_COOL, "COOL", "COOL", "ON", set_frame({{6, 0x03}, {8, 0x01}, {9, 0x03}})},
        {CLIMATE_MODE_FAN_ONLY, "FAN_ONLY", "FAN", "ON", set_frame({{6, 0x03}, {8, 0x01}, {9, 0x07}})},
        {CLIMATE_MODE_AUTO, "AUTO", "AUTO", "ON", set_frame({{6, 0x03}, {8, 0x01}, {9, 0x08}})},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.label);
        hp().make_call().set_mode(c.mode).perform();
        EXPECT_EQ(str(hp().wantedSettings.mode), c.wanted_mode);
        EXPECT_EQ(str(hp().wantedSettings.power), c.wanted_power);
        EXPECT_EQ(hp().mode, c.mode) << "the climate entity takes the requested mode at once";
        EXPECT_FLOAT_EQ(hp().wantedSettings.temperature, -1.0f) << "a mode-only change carries no temperature";

        const auto frame = step_until_set_frame();
        EXPECT_EQ(frame, c.frame) << hex(frame);
        run_poll_cycle(responses_);
    }
}

// HEAT_COOL is a Home Assistant side mode (two sliders): it is sent to the unit as the hardware AUTO.
class ClimateControlsHeatCool : public ClimateControls {
   protected:
    void before_setup(esphome::CN105Climate& hp) override { hp.config_traits().add_supported_mode(CLIMATE_MODE_HEAT_COOL); }
};

TEST_F(ClimateControlsHeatCool, Characterize_ModeMappingHeatCoolIsHardwareAuto) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());

    hp().make_call().set_mode(CLIMATE_MODE_HEAT_COOL).perform();
    EXPECT_EQ(str(hp().wantedSettings.mode), "AUTO") << "HEAT_COOL -> hardware AUTO";
    EXPECT_EQ(str(hp().wantedSettings.power), "ON");
    EXPECT_EQ(hp().mode, CLIMATE_MODE_HEAT_COOL) << "the entity itself stays in HEAT_COOL";
    ASSERT_EQ(publishes_.size(), 1u);
    EXPECT_EQ(publishes_[0].mode, CLIMATE_MODE_HEAT_COOL);

    const auto frame = step_until_set_frame();
    EXPECT_EQ(frame, set_frame({{6, 0x03}, {8, 0x01}, {9, 0x08}})) << hex(frame) << " (same bytes as AUTO)";
}

TEST_F(ClimateControls, Characterize_FanMapping) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());

    // Flag 0x08 sets the fan, byte 11 carries FAN[] = {AUTO 0x00, QUIET 0x01, "1" 0x02, "2" 0x03, "3" 0x05, "4" 0x06}.
    const struct {
        ClimateFanMode fan;
        const char* label;
        const char* wanted_fan;
        uint8_t byte11;
    } cases[] = {
        {CLIMATE_FAN_AUTO, "AUTO", "AUTO", 0x00},
        {CLIMATE_FAN_LOW, "LOW", "1", 0x02},
        {CLIMATE_FAN_MEDIUM, "MEDIUM", "2", 0x03},
        {CLIMATE_FAN_HIGH, "HIGH", "4", 0x06},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.label);
        hp().make_call().set_fan_mode(c.fan).perform();
        EXPECT_EQ(str(hp().wantedSettings.fan), c.wanted_fan);
        EXPECT_EQ(str(hp().wantedSettings.mode), "<null>") << "a fan change touches nothing else";
        EXPECT_EQ(str(hp().wantedSettings.power), "<null>");
        ASSERT_TRUE(hp().fan_mode.has_value());
        EXPECT_EQ(*hp().fan_mode, c.fan);

        const auto frame = step_until_set_frame();
        EXPECT_EQ(frame, set_frame({{6, 0x08}, {11, c.byte11}})) << hex(frame);
        run_poll_cycle(responses_);
    }
}

// The fixture advertises four fan modes; the others are advertised here to pin how control_fan maps them.
class ClimateControlsExtraFans : public ClimateControls {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        for (auto fan : {CLIMATE_FAN_QUIET, CLIMATE_FAN_MIDDLE, CLIMATE_FAN_DIFFUSE, CLIMATE_FAN_ON, CLIMATE_FAN_OFF}) {
            hp.config_traits().add_supported_fan_mode(fan);
        }
    }
};

TEST_F(ClimateControlsExtraFans, Characterize_FanMappingExtraModes) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());

    const struct {
        ClimateFanMode fan;
        const char* label;
        const char* wanted_fan;
        uint8_t byte11;
    } cases[] = {
        {CLIMATE_FAN_QUIET, "QUIET", "QUIET", 0x01},
        {CLIMATE_FAN_MIDDLE, "MIDDLE", "3", 0x05},
        {CLIMATE_FAN_DIFFUSE, "DIFFUSE", "QUIET", 0x01},  // DIFFUSE is sent as QUIET
        {CLIMATE_FAN_ON, "ON", "AUTO", 0x00},             // ON is sent as AUTO
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.label);
        hp().make_call().set_fan_mode(c.fan).perform();
        EXPECT_EQ(str(hp().wantedSettings.fan), c.wanted_fan);
        const auto frame = step_until_set_frame();
        EXPECT_EQ(frame, set_frame({{6, 0x08}, {11, c.byte11}})) << hex(frame);
        run_poll_cycle(responses_);
    }

    // FAN_OFF is not a fan speed at all: control_fan turns the whole unit OFF (power flag, no fan flag).
    hp().make_call().set_fan_mode(CLIMATE_FAN_OFF).perform();
    EXPECT_EQ(str(hp().wantedSettings.power), "OFF");
    EXPECT_EQ(str(hp().wantedSettings.fan), "<null>");
    const auto frame = step_until_set_frame();
    EXPECT_EQ(frame, set_frame({{6, 0x01}})) << hex(frame);
}

TEST_F(ClimateControls, Characterize_SwingMapping) {
    // Start from a real state whose vane is already swinging: SWING_OFF only acts on a swinging vane.
    connect_and_poll(cn105_test::real_frames::SETTINGS_FAN2_VANE_SWING);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(str(hp().currentSettings.vane), "SWING");

    // Flag 0x10 (byte 6) sets the vane; byte 12 carries VANE[] = {AUTO 0x00, ... "↑" 0x02, ..., SWING 0x07}.
    hp().make_call().set_swing_mode(CLIMATE_SWING_OFF).perform();
    EXPECT_EQ(str(hp().wantedSettings.vane), "AUTO") << "swing OFF puts a swinging vane back to AUTO";
    EXPECT_EQ(str(hp().wantedSettings.wideVane), "<null>") << "no horizontal vane on this unit";
    EXPECT_EQ(hp().swing_mode, CLIMATE_SWING_OFF);
    auto frame = step_until_set_frame();
    EXPECT_EQ(frame, set_frame({{6, 0x10}, {12, 0x00}})) << hex(frame);
    run_poll_cycle(responses_);

    hp().make_call().set_swing_mode(CLIMATE_SWING_VERTICAL).perform();
    EXPECT_EQ(str(hp().wantedSettings.vane), "SWING");
    EXPECT_EQ(str(hp().wantedSettings.wideVane), "<null>");
    EXPECT_EQ(hp().swing_mode, CLIMATE_SWING_VERTICAL);
    frame = step_until_set_frame();
    EXPECT_EQ(frame, set_frame({{6, 0x10}, {12, 0x07}})) << hex(frame);
    run_poll_cycle(responses_);

}

TEST_F(ClimateControls, Characterize_TemperatureChangeOnly) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());

    hp().make_call().set_target_temperature(24.0f).perform();
    EXPECT_FLOAT_EQ(hp().wantedSettings.temperature, 24.0f);
    EXPECT_EQ(str(hp().wantedSettings.mode), "<null>") << "a temperature-only call does not touch mode/power";
    EXPECT_EQ(str(hp().wantedSettings.power), "<null>");
    EXPECT_TRUE(hp().wantedSettings.hasChanged);
    EXPECT_FLOAT_EQ(hp().wantedSettings.last_user_temperature, 24.0f);
    ASSERT_EQ(publishes_.size(), 1u);
    EXPECT_FLOAT_EQ(publishes_[0].target_temperature, 24.0f);

    const auto frame = step_until_set_frame();
    EXPECT_EQ(frame, set_frame({{6, 0x04}, {19, 0xB0}})) << hex(frame);
}

// ════════════════════════════════════════════════════════════════
// Send gating: debounce, hasBeenSent short-circuit, 300 ms write throttle
// ════════════════════════════════════════════════════════════════

// A configured debounce delay (set_debounce_delay, normally from the YAML `debounce_delay`).
class ClimateControlsDebounce : public ClimateControls {
   protected:
    void before_setup(esphome::CN105Climate& hp) override { hp.set_debounce_delay(500); }
};

TEST_F(ClimateControlsDebounce, Characterize_DebounceBoundary) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(hp().debounce_delay_, 500u);

    const uint32_t t_command = cn105_test::TestClock::now();
    hp().make_call().set_mode(CLIMATE_MODE_COOL).perform();
    ASSERT_EQ(hp().wantedSettings.lastChange, static_cast<long>(t_command));

    // Nothing is written while `now - lastChange < 500`, including at +490 ms ...
    for (uint32_t elapsed = 10; elapsed <= 490; elapsed += 10) {
        step(10);
        ASSERT_EQ(count_set_frames_in(take_tx_frames()), 0u) << "SET frame too early at +" << elapsed << " ms";
    }
    ASSERT_EQ(cn105_test::TestClock::now(), t_command + 490);
    EXPECT_TRUE(hp().wantedSettings.hasChanged);
    EXPECT_FALSE(hp().wantedSettings.hasBeenSent);

    // ... and the first tick where `now - lastChange == 500` is not `< 500` any more: the frame goes out.
    step(10);
    const auto frames = take_tx_frames();
    ASSERT_EQ(count_set_frames_in(frames), 1u);
    EXPECT_EQ(cn105_test::TestClock::now(), t_command + 500) << "first send exactly lastChange + debounce_delay";
    EXPECT_EQ(hp().lastSend, static_cast<unsigned long>(t_command + 500));
}

TEST_F(ClimateControls, Characterize_HasBeenSentShortCircuits) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());
    run_for(400);  // the last info request is older than the 300 ms throttle (no poll cycle starts yet)
    take_tx();

    // A change that is ready to go (debounce 0, throttle elapsed), but already flagged as in flight.
    hp().setModeSetting("COOL");
    hp().setPowerSetting("ON");
    hp().wantedSettings.hasChanged = true;
    hp().wantedSettings.lastChange = static_cast<long>(cn105_test::TestClock::now()) - 1000;
    hp().wantedSettings.hasBeenSent = true;

    hp().checkPendingWantedSettings();
    EXPECT_TRUE(take_tx().empty()) << "hasBeenSent short-circuits before any other test";
    EXPECT_TRUE(hp().wantedSettings.hasChanged) << "and leaves the flags untouched";
    EXPECT_TRUE(hp().wantedSettings.hasBeenSent);

    // The same state with hasBeenSent cleared is sent at once.
    hp().wantedSettings.hasBeenSent = false;
    hp().checkPendingWantedSettings();
    const auto frames = take_tx_frames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], set_frame({{6, 0x03}, {8, 0x01}, {9, 0x03}})) << hex(frames[0]);
}

TEST_F(ClimateControls, Characterize_ThrottleWithin300msOfLastSend) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());
    run_for(400);  // the throttle of the last info request has elapsed
    take_tx();

    // Another write has just happened: lastSend = now.
    hp().buildAndSendInfoPacket(0x02);
    take_tx();
    const unsigned long last_send = hp().lastSend;
    ASSERT_EQ(last_send, static_cast<unsigned long>(cn105_test::TestClock::now()));

    hp().make_call().set_mode(CLIMATE_MODE_COOL).perform();

    // checkPendingWantedSettings() returns silently while `now - lastSend <= 300` (strict at 300)...
    for (uint32_t elapsed = 10; elapsed <= 300; elapsed += 10) {
        step(10);
        ASSERT_EQ(count_set_frames_in(take_tx_frames()), 0u) << "SET frame too early at +" << elapsed << " ms";
    }
    EXPECT_TRUE(hp().wantedSettings.hasChanged);
    EXPECT_FALSE(hp().wantedSettings.hasBeenSent);

    // ... and sends on the first tick past it.
    step(10);
    ASSERT_EQ(count_set_frames_in(take_tx_frames()), 1u);
    EXPECT_EQ(hp().lastSend, last_send + 310);
}

// ════════════════════════════════════════════════════════════════
// BUG-08: signed `long now` in the debounce / throttle arithmetic
// ════════════════════════════════════════════════════════════════

// The clock starts so that the command is issued ~100 ms before millis() reaches 2^31, i.e. just before
// `long now = esphome::millis()` turns negative on a 32-bit target (-m32: long is 32 bits). The connection
// handshake and one poll cycle take ~11.1 s of test time, hence the offset.
class ClimateControlsClockNear2Pow31 : public ClimateControlsDebounce {
   protected:
    cn105_test::Cn105Options options() const override {
        cn105_test::Cn105Options opts;
        opts.start_ms = 0x80000000ull - 11170ull;
        return opts;
    }
};

// Pins current buggy behaviour — flip in Phase 4 (TIME-03)
// BUG-08: checkPendingWantedSettings() stores millis() in a signed `long` and subtracts signed values
// (`now - wantedSettings.lastChange < debounce_delay_`). Past 2^31 ms (24.8 days of uptime) `now` is
// negative and lastChange (set before the crossing) is positive: the difference only comes out right
// because the 32-bit subtraction wraps (undefined behaviour in C++). This pins what the -m32 build does.
TEST_F(ClimateControlsClockNear2Pow31, Characterize_BUG08_DebounceSignedNowPast2Pow31) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());
    const uint32_t t_command = cn105_test::TestClock::now();
    ASSERT_LT(t_command, 0x80000000u) << "the command must be issued before the 2^31 crossing";
    ASSERT_GT(t_command, 0x80000000u - 400u) << "and close to it (retune start_ms otherwise)";

    hp().make_call().set_mode(CLIMATE_MODE_COOL).perform();
    ASSERT_GT(hp().wantedSettings.lastChange, 0L) << "lastChange is still positive";

    const auto frame = step_until_set_frame();
    ASSERT_FALSE(frame.empty()) << "the SET frame is sent even though `long now` went negative";
    EXPECT_EQ(frame, set_frame({{6, 0x03}, {8, 0x01}, {9, 0x03}})) << hex(frame);
    EXPECT_GE(sent_at_ms_, 0x80000000u) << "sent after the crossing";
    EXPECT_LT(static_cast<long>(sent_at_ms_), 0L) << "where `long now` is negative";
    EXPECT_EQ(sent_at_ms_, t_command + 500) << "with the same timing as far from 2^31: lastChange + debounce_delay";
    EXPECT_EQ(hp().lastSend, static_cast<unsigned long>(sent_at_ms_));
}
