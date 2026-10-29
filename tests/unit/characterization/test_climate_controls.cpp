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
