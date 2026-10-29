/// test_profile_state.cpp — Characterization of protocol-profile state handling
/// Role: pins, on the unmodified component, how HeatPumpProfile (heatpump_profile.cpp) and LossnayProfile
///       (lossnay_profile.cpp) turn received frames into currentSettings / published Home Assistant state,
///       how the guards that protect a user demand behave (pending temperature, post-send grace, received
///       setpoint grace, vane grace), the action/stage derivation, the dual-setpoint (HEAT_COOL) handling
///       and setpoint persistence, and the Lossnay restrictions. Phases 6 (readback confirmation) and 8
///       (enum class + encapsulation) rework exactly this state handling.
///       Every frame fed to the component is a real_frames.h constant (D-23: no synthetic frame here).
/// Deps: fixture.h, real_frames.h, heatpump_profile.h, lossnay_profile.h, cn105_protocol.h, cn105.h

#include "fixture.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "cn105_protocol.h"
#include "heatpump_profile.h"
#include "lossnay_profile.h"
#include "real_frames.h"

namespace {

using cn105_test::hex;
using namespace esphome::climate;

/// "<null>" for a nullptr C string, so a failed assertion prints something readable.
std::string str(const char* s) { return s == nullptr ? std::string("<null>") : std::string(s); }

}  // namespace

class ProfileState : public cn105_test::Cn105Fixture {
   protected:
    /// Runs one poll cycle where the heat pump answers 0x02 with `settings` (the other codes keep their
    /// default real answers).
    void poll_with_settings(const std::vector<uint8_t>& settings) {
        auto responses = default_responses();
        responses[0x02] = settings;
        run_poll_cycle(responses);
    }
};

// ════════════════════════════════════════════════════════════════
// Received settings, end to end (tracer)
// ════════════════════════════════════════════════════════════════

TEST_F(ProfileState, Characterize_DecodeRealSettingsUpdatesCurrentAndPublishes) {
    connect();
    ASSERT_FALSE(HasFatalFailure());

    // --- real 0x02 "ON, HEAT, 21.5, fan AUTO, vane AUTO" ----------------------------------------------
    poll_with_settings(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5);
    EXPECT_EQ(str(hp().currentSettings.power), "ON");
    EXPECT_EQ(str(hp().currentSettings.mode), "HEAT");
    EXPECT_FLOAT_EQ(hp().currentSettings.temperature, 21.5f);
    EXPECT_EQ(str(hp().currentSettings.fan), "AUTO");
    EXPECT_EQ(str(hp().currentSettings.vane), "AUTO");
    EXPECT_FALSE(hp().currentSettings.iSee);
    EXPECT_TRUE(hp().currentSettings.connected);

    ASSERT_FALSE(publishes_.empty());
    const auto on = publishes_.back();
    EXPECT_EQ(on.mode, CLIMATE_MODE_HEAT);
    EXPECT_FLOAT_EQ(on.target_temperature, 21.5f);
    EXPECT_FLOAT_EQ(on.current_temperature, 23.0f);  // from the real 0x03 room temperature answer
    ASSERT_TRUE(on.fan_mode.has_value());
    EXPECT_EQ(*on.fan_mode, CLIMATE_FAN_AUTO);
    EXPECT_EQ(on.swing_mode, CLIMATE_SWING_OFF);

    // --- real 0x02 "OFF, HEAT, 19.5" --------------------------------------------------------------------
    publishes_.clear();
    poll_with_settings(cn105_test::real_frames::SETTINGS_OFF_HEAT_19_5);
    EXPECT_EQ(str(hp().currentSettings.power), "OFF");
    EXPECT_EQ(str(hp().currentSettings.mode), "HEAT");
    EXPECT_FLOAT_EQ(hp().currentSettings.temperature, 19.5f);

    ASSERT_FALSE(publishes_.empty());
    const auto off = publishes_.back();
    EXPECT_EQ(off.mode, CLIMATE_MODE_OFF);
    EXPECT_FLOAT_EQ(off.target_temperature, 19.5f);
    EXPECT_EQ(off.action, CLIMATE_ACTION_OFF);
}
