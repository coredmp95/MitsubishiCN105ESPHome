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
#include <map>
#include <string>
#include <vector>

#include "cn105_protocol.h"
#include "heatpump_profile.h"
#include "lossnay_profile.h"
#include "real_frames.h"
#include "stage_sensor.h"
#include "sub_mode_sensor.h"

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

    /// Like poll_with_settings() but also overrides the 0x09 (sub-mode / stage) answer.
    void poll_with_settings_and_power(const std::vector<uint8_t>& settings, const std::vector<uint8_t>& power) {
        auto responses = default_responses();
        responses[0x02] = settings;
        responses[0x09] = power;
        run_poll_cycle(responses);
    }

    /// Steps by 10 ms until a 0x41 frame is written (max `max_ms`); returns it (empty when none) and leaves
    /// the time it was written at in `sent_at_ms_`. Other frames written meanwhile are dropped.
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

    /// The heat-pump profile of the component (this suite never runs Lossnay).
    esphome::cn105::HeatPumpProfile& profile() {
        return *static_cast<esphome::cn105::HeatPumpProfile*>(hp().profile_.get());
    }

    /// One poll cycle where the heat pump answers like run_poll_cycle(), except that `hook` runs right
    /// after the request for `hook_code` was written and before its answer is fed: the moment where a
    /// user command meets a response already on its way. (While a command waits, loop() starts no new
    /// poll cycle, so a received 0x02 can only meet a pending command this way.)
    void poll_with_hook(const std::map<uint8_t, std::vector<uint8_t>>& responses, uint8_t hook_code,
                        const std::function<void()>& hook, uint32_t max_ms = 5000) {
        bool started = false;
        bool hooked = false;
        for (uint32_t elapsed = 0; elapsed < max_ms; elapsed += 10) {
            step(10);
            for (const auto& frame : take_tx_frames()) {
                if (frame.size() < 6 || frame[1] != 0x42) continue;
                if (frame[5] == hook_code && !hooked) {
                    hooked = true;
                    hook();
                }
                auto it = responses.find(frame[5]);
                if (it != responses.end()) feed(it->second);
            }
            if (hp().loopCycle.isCycleRunning()) started = true;
            if (started && !hp().loopCycle.isCycleRunning()) break;
        }
        ASSERT_TRUE(hooked) << "the request for the hook code was never written";
    }

    uint32_t sent_at_ms_ = 0;
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

// ════════════════════════════════════════════════════════════════
// Guards protecting a user demand (heatpump_profile.cpp: apply_received_settings)
// ════════════════════════════════════════════════════════════════

// Observed: while a user temperature is pending the incoming setpoint IS ignored, but silently. The first
// line of shouldApplyIncomingSetpoint() is `wantedSettings.temperature != -1 -> return false`, which
// returns before hasPendingUserTemperature() (which needs that same field != -1) is ever consulted, so the
// log "Ignoring setpoint: pending user temp" can never be written. Pinned as observed (dead log line).
TEST_F(ProfileState, Characterize_IncomingSetpointIgnoredWhilePendingUserTemp) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    poll_with_settings(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5);
    ASSERT_FLOAT_EQ(hp().currentSettings.temperature, 21.5f);

    publishes_.clear();
    cn105_test::TestLog::clear();

    // The user command lands while the 0x02 request is in flight; the heat pump answers 21.5 with another
    // fan and vane (real frame), so the settings differ from currentSettings and the profile processes them.
    auto responses = default_responses();
    responses[0x02] = cn105_test::real_frames::SETTINGS_FAN2_VANE_UP;
    bool pending_when_answer_arrives = false;
    poll_with_hook(responses, 0x02, [&] {
        hp().make_call().set_target_temperature(24.0f).perform();
        pending_when_answer_arrives = profile().hasPendingUserTemperature();
    });
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_TRUE(pending_when_answer_arrives);

    EXPECT_EQ(str(hp().currentSettings.fan), "2") << "the fan from the frame was applied";
    EXPECT_FLOAT_EQ(hp().currentSettings.temperature, 21.5f) << "the incoming setpoint was not applied";
    EXPECT_FLOAT_EQ(hp().target_temperature, 24.0f) << "the user's pending target stays";
    ASSERT_FALSE(publishes_.empty());
    for (const auto& p : publishes_) EXPECT_FLOAT_EQ(p.target_temperature, 24.0f);
    EXPECT_TRUE(cn105_test::TestLog::contains("Settings changed, updating HA states")) << "the frame was processed";
    EXPECT_TRUE(cn105_test::TestLog::contains("Climate action is")) << "debug logs are captured: absence below is meaningful";
    EXPECT_FALSE(cn105_test::TestLog::contains("Ignoring setpoint: pending user temp"))
        << "dead log line: the earlier wantedSettings.temperature != -1 test returns first";
    EXPECT_FALSE(cn105_test::TestLog::contains("Ignoring setpoint"));
}

// Observed: sendWantedSettingsDelegate() sets hasBeenSent = true and, in the same call, resetSettings()
// puts it back to false. isWithinPostSendGrace() needs hasBeenSent, so through the production flow the
// "post-send grace" window can never be open; what protects the user's value right after a send is the
// received-setpoint grace of the next test. The grace helper itself is pinned here by forcing the flag,
// the only way to reach it. Phase 6 (readback confirmation) reworks this.
TEST_F(ProfileState, Characterize_IncomingSetpointIgnoredWithinPostSendGrace) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    poll_with_settings(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5);

    hp().make_call().set_target_temperature(24.0f).perform();
    const auto frame = step_until_set_frame();
    ASSERT_FALSE(frame.empty());
    const long last_change = hp().wantedSettings.lastChange;

    // --- through the real flow: no grace is open after the send ------------------------------------------
    EXPECT_FALSE(hp().wantedSettings.hasBeenSent) << "reset by the same call that sent the frame";
    EXPECT_FALSE(profile().isWithinPostSendGrace());
    EXPECT_FALSE(profile().hasPendingUserTemperature());
    EXPECT_FLOAT_EQ(hp().wantedSettings.temperature, -1.0f);

    // --- forced state: hasBeenSent true, update_interval 1000 ms + 750 ms -> grace of 1750 ms ---------------
    hp().wantedSettings.hasBeenSent = true;
    heatpumpSettings incoming{};
    incoming.temperature = 21.5f;

    cn105_test::TestClock::set(static_cast<uint64_t>(last_change) + 1749);
    cn105_test::TestLog::clear();
    EXPECT_TRUE(profile().isWithinPostSendGrace());
    EXPECT_FALSE(profile().shouldApplyIncomingSetpoint(incoming));
    EXPECT_TRUE(cn105_test::TestLog::contains("Ignoring setpoint: post-send grace"));

    cn105_test::TestClock::set(static_cast<uint64_t>(last_change) + 1750);
    cn105_test::TestLog::clear();
    EXPECT_FALSE(profile().isWithinPostSendGrace()) << "strict < : the grace ends at lastChange + 1750 ms";
    EXPECT_FALSE(cn105_test::TestLog::contains("post-send grace"));
}

// After a send, the user's value is protected by the received-setpoint grace: for 3000 ms after the
// command (RECEIVED_SETPOINT_GRACE_WINDOW_MS) an incoming setpoint that differs by more than 0.5 degree
// from the last user setpoint is ignored, with a log line. After it, the heat pump's value wins again.
TEST_F(ProfileState, Characterize_IncomingSetpointDisagreesWithLastUser) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    poll_with_settings(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5);

    hp().make_call().set_target_temperature(24.0f).perform();
    const uint32_t t_command = static_cast<uint32_t>(hp().wantedSettings.lastChange);
    ASSERT_FALSE(step_until_set_frame().empty());
    EXPECT_EQ(hp().wantedSettings.last_user_temperature_ms, t_command);
    EXPECT_FLOAT_EQ(hp().wantedSettings.last_user_temperature, 24.0f);
    publishes_.clear();
    cn105_test::TestLog::clear();

    // --- inside the window: the heat pump still reports 21.5 (real frame, fan 2 / vane up) ------------------
    poll_with_settings(cn105_test::real_frames::SETTINGS_FAN2_VANE_UP);
    ASSERT_LT(cn105_test::TestClock::now() - t_command, 3000u) << "the poll must end inside the grace window";
    EXPECT_TRUE(cn105_test::TestLog::contains("Ignoring setpoint: disagrees with user (21.5 vs 24.0)"));
    EXPECT_FALSE(cn105_test::TestLog::contains("post-send grace"));
    EXPECT_FLOAT_EQ(hp().currentSettings.temperature, 21.5f) << "unchanged: the setpoint was not applied";
    EXPECT_EQ(str(hp().currentSettings.fan), "2") << "the rest of the frame was applied";
    EXPECT_FLOAT_EQ(hp().target_temperature, 24.0f);
    ASSERT_FALSE(publishes_.empty());
    EXPECT_FLOAT_EQ(publishes_.back().target_temperature, 24.0f);

    // --- the boundary of the window: strict < 3000 ms, measured from the command time ------------------------
    heatpumpSettings incoming{};
    incoming.temperature = 21.5f;
    cn105_test::TestClock::set(static_cast<uint64_t>(t_command) + 2999);
    EXPECT_FALSE(profile().shouldApplyIncomingSetpoint(incoming));
    cn105_test::TestClock::set(static_cast<uint64_t>(t_command) + 3000);
    EXPECT_TRUE(profile().shouldApplyIncomingSetpoint(incoming));

    // --- after the window: the heat pump's 21.5 replaces the user's 24 -----------------------------------------
    cn105_test::TestLog::clear();
    publishes_.clear();
    poll_with_settings(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5);  // fan 2 -> AUTO: differs from current
    EXPECT_FALSE(cn105_test::TestLog::contains("Ignoring setpoint"));
    EXPECT_FLOAT_EQ(hp().target_temperature, 21.5f);
    ASSERT_FALSE(publishes_.empty());
    EXPECT_FLOAT_EQ(publishes_.back().target_temperature, 21.5f);
}

// A user vane (swing) command is protected by the same 3000 ms window: an incoming vane that differs from
// the last user vane is not applied (no log line exists for this guard; the observable is the swing mode
// and currentSettings.vane).
TEST_F(ProfileState, Characterize_IncomingVaneIgnoredWithinGrace) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    poll_with_settings(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5);
    ASSERT_EQ(str(hp().currentSettings.vane), "AUTO");

    hp().make_call().set_swing_mode(CLIMATE_SWING_VERTICAL).perform();
    const uint32_t t_command = static_cast<uint32_t>(hp().wantedSettings.lastChange);
    ASSERT_FALSE(step_until_set_frame().empty());
    EXPECT_EQ(str(hp().wantedSettings.last_user_vane), "SWING");
    EXPECT_EQ(hp().wantedSettings.last_user_vane_ms, t_command);
    EXPECT_EQ(hp().swing_mode, CLIMATE_SWING_VERTICAL) << "optimistic: set when the SET frame left";

    // --- inside the window: the heat pump still reports the old vane position "up" (real frame) --------------
    publishes_.clear();
    poll_with_settings(cn105_test::real_frames::SETTINGS_FAN2_VANE_UP);
    ASSERT_LT(cn105_test::TestClock::now() - t_command, 3000u);
    EXPECT_EQ(str(hp().currentSettings.fan), "2") << "the frame was processed";
    EXPECT_EQ(str(hp().currentSettings.vane), "AUTO") << "incoming vane ignored: currentSettings keeps its vane";
    EXPECT_EQ(hp().swing_mode, CLIMATE_SWING_VERTICAL);
    ASSERT_FALSE(publishes_.empty());
    EXPECT_EQ(publishes_.back().swing_mode, CLIMATE_SWING_VERTICAL);

    // --- after the window the same frame is applied -------------------------------------------------------------
    run_for(3000);
    publishes_.clear();
    poll_with_settings(cn105_test::real_frames::SETTINGS_FAN2_VANE_UP);
    EXPECT_EQ(str(hp().currentSettings.vane), "\xe2\x86\x91");  // "up" arrow, UTF-8
    EXPECT_EQ(hp().swing_mode, CLIMATE_SWING_OFF);
    ASSERT_FALSE(publishes_.empty());
    EXPECT_EQ(publishes_.back().swing_mode, CLIMATE_SWING_OFF);
}

// ════════════════════════════════════════════════════════════════
// Action / stage / sub-mode (update_action, decode_submode)
// ════════════════════════════════════════════════════════════════

/// Stage and sub-mode sensors attached, stage used as operating fallback (use_stage_for_operating_status).
class ProfileStateStage : public ProfileState {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        hp.set_stage_sensor(&stage_sensor_);
        hp.set_sub_mode_sensor(&sub_mode_sensor_);
        hp.set_use_stage_for_operating_status(true);
    }
    esphome::StageSensor stage_sensor_;
    esphome::SubModSensor sub_mode_sensor_;
};

TEST_F(ProfileStateStage, Characterize_UpdateActionFromStatusAndPower) {
    connect();
    ASSERT_FALSE(HasFatalFailure());

    // --- real 0x06 STATUS_IDLE (operating 0) + real 0x09 POWER_IDLE (stage IDLE): HEAT but idle ---------------
    poll_with_settings_and_power(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5, cn105_test::real_frames::POWER_IDLE);
    EXPECT_FALSE(hp().currentStatus.operating);
    EXPECT_EQ(str(hp().currentSettings.stage), "IDLE");
    EXPECT_EQ(str(hp().currentSettings.sub_mode), "NORMAL");
    EXPECT_EQ(hp().mode, CLIMATE_MODE_HEAT);
    EXPECT_EQ(hp().action, CLIMATE_ACTION_IDLE);
    EXPECT_EQ(stage_sensor_.state, "IDLE");
    EXPECT_EQ(sub_mode_sensor_.state, "NORMAL");

    // --- real 0x09 POWER_LOW (stage LOW) with STATUS_IDLE still saying "not operating": the stage fallback
    //     makes the unit count as operating -> HEATING, published to Home Assistant ---------------------------
    publishes_.clear();
    poll_with_settings_and_power(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5, cn105_test::real_frames::POWER_LOW);
    EXPECT_FALSE(hp().currentStatus.operating) << "the 0x06 status alone still says idle";
    EXPECT_EQ(str(hp().currentSettings.stage), "LOW");
    EXPECT_EQ(stage_sensor_.state, "LOW");
    EXPECT_EQ(hp().action, CLIMATE_ACTION_HEATING);
    ASSERT_FALSE(publishes_.empty());
    EXPECT_EQ(publishes_.back().action, CLIMATE_ACTION_HEATING);

    // --- real 0x09 POWER_OFF_SUBMODE_0x10 (sub-mode OFF, stage IDLE) ----------------------------------------------
    publishes_.clear();
    poll_with_settings_and_power(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5, cn105_test::real_frames::POWER_OFF_SUBMODE_0x10);
    EXPECT_EQ(str(hp().currentSettings.stage), "IDLE");
    EXPECT_EQ(str(hp().currentSettings.sub_mode), "OFF");
    EXPECT_EQ(sub_mode_sensor_.state, "OFF");
    EXPECT_EQ(hp().action, CLIMATE_ACTION_IDLE) << "stage back to IDLE, HEAT mode still on";
}

// Without the stage fallback, the same POWER_LOW frame changes nothing in the action: only the 0x06
// "operating" byte (no real captured frame has it set) can make the unit HEATING.
TEST_F(ProfileState, Characterize_ActionIgnoresStageWithoutFallback) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    poll_with_settings_and_power(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5, cn105_test::real_frames::POWER_LOW);
    EXPECT_EQ(hp().mode, CLIMATE_MODE_HEAT);
    EXPECT_EQ(hp().action, CLIMATE_ACTION_IDLE);
}

// ════════════════════════════════════════════════════════════════
// Dual setpoint (HEAT_COOL) and setpoint persistence
// ════════════════════════════════════════════════════════════════

/// supports dual_setpoint: HEAT_COOL mode and the two-point target traits, as climate.py configures them.
class ProfileStateDualSetpoint : public ProfileState {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        auto& traits = hp.config_traits();
        traits.add_supported_mode(CLIMATE_MODE_HEAT_COOL);
        traits.add_feature_flags(CLIMATE_REQUIRES_TWO_POINT_TARGET_TEMPERATURE);
        configure(hp);
    }
    virtual void configure(esphome::CN105Climate& hp) { (void) hp; }

    /// Connects, polls one real "ON, HEAT, 21.5" state and forgets what was recorded so far.
    void connect_and_poll() {
        connect();
        if (HasFatalFailure()) return;
        poll_with_settings(cn105_test::real_frames::SETTINGS_ON_HEAT_21_5);
        publishes_.clear();
        take_tx();
    }
};

TEST_F(ProfileStateDualSetpoint, Characterize_HeatCoolDualSetpoint) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(hp().supports_dual_setpoint_);
    // HEAT with a two-point trait: the received setpoint is the low bound. The high bound is not 21.5: update_action()
    // runs sanitize_setpoints() BEFORE the setpoint is reconciled, finds both bounds NaN and no known
    // temperature yet, and seeds the defaults 18.0 - 22.0; reconcile then only overwrites the low bound.
    EXPECT_FLOAT_EQ(hp().target_temperature_low, 21.5f);
    EXPECT_FLOAT_EQ(hp().target_temperature_high, 22.0f);

    // --- HEAT_COOL with a band: the unit gets AUTO and ONE setpoint (the deadband applied to the room temp) ---
    hp().make_call().set_mode(CLIMATE_MODE_HEAT_COOL).set_target_temperature_low(20.0f).set_target_temperature_high(24.0f).perform();
    EXPECT_EQ(str(hp().wantedSettings.mode), "AUTO");
    EXPECT_EQ(str(hp().wantedSettings.power), "ON");
    // room temperature 23.0 is inside [20, 24]: deadband_setting() keeps it
    EXPECT_FLOAT_EQ(hp().wantedSettings.temperature, 23.0f);
    EXPECT_EQ(hp().mode, CLIMATE_MODE_HEAT_COOL);
    ASSERT_FALSE(publishes_.empty());
    EXPECT_EQ(publishes_.back().mode, CLIMATE_MODE_HEAT_COOL);
    EXPECT_FLOAT_EQ(publishes_.back().target_temperature_low, 20.0f);
    EXPECT_FLOAT_EQ(publishes_.back().target_temperature_high, 24.0f);
    EXPECT_FLOAT_EQ(hp().currentSettings.dual_low_target, 20.0f);
    EXPECT_FLOAT_EQ(hp().currentSettings.dual_high_target, 24.0f);

    const auto frame = step_until_set_frame();
    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(frame[6], 0x07) << hex(frame);   // power + mode + temperature flags
    EXPECT_EQ(frame[9], 0x08) << hex(frame);   // hardware AUTO
    EXPECT_EQ(frame[19], 0xAE) << hex(frame);  // 23.0 * 2 + 128: a single setpoint, not the band
    publishes_.clear();

    // --- low only: the band's low bound moves, the high bound stays (HEAT_COOL, not AUTO) --------------------
    run_for(1000);
    hp().make_call().set_target_temperature_low(19.0f).perform();
    EXPECT_FLOAT_EQ(hp().target_temperature_low, 19.0f);
    EXPECT_FLOAT_EQ(hp().target_temperature_high, 24.0f);
    ASSERT_FALSE(publishes_.empty());
    EXPECT_FLOAT_EQ(publishes_.back().target_temperature_low, 19.0f);
    EXPECT_FLOAT_EQ(publishes_.back().target_temperature_high, 24.0f);
    ASSERT_FALSE(step_until_set_frame().empty());

    // --- high only, right after the low change: UI anti-rebound (600 ms) ignores it -----------------------------
    publishes_.clear();
    cn105_test::TestLog::clear();
    hp().make_call().set_target_temperature_high(25.0f).perform();
    EXPECT_TRUE(cn105_test::TestLog::contains("ignored high setpoint due to UI anti-rebound after low change"));
    EXPECT_FLOAT_EQ(hp().target_temperature_high, 24.0f);

    // --- high only after the anti-rebound delay: applied --------------------------------------------------------
    run_for(700);
    cn105_test::TestLog::clear();
    hp().make_call().set_target_temperature_high(25.0f).perform();
    EXPECT_FALSE(cn105_test::TestLog::contains("anti-rebound"));
    EXPECT_FLOAT_EQ(hp().target_temperature_low, 19.0f);
    EXPECT_FLOAT_EQ(hp().target_temperature_high, 25.0f);
    ASSERT_FALSE(step_until_set_frame().empty());
}

class ProfileStateRestoreSetpoints : public ProfileStateDualSetpoint {
   protected:
    void configure(esphome::CN105Climate& hp) override { hp.set_restore_setpoints(true); }
};

TEST_F(ProfileStateRestoreSetpoints, Characterize_SetpointsSavedAndRestored) {
    connect_and_poll();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(cn105_test::TestPrefs::size(), 0u) << "setup(): nothing saved yet";

    // --- a HEAT_COOL band is saved when the command is received ---------------------------------------------------
    hp().make_call().set_mode(CLIMATE_MODE_HEAT_COOL).set_target_temperature_low(20.0f).set_target_temperature_high(24.0f).perform();
    EXPECT_EQ(cn105_test::TestPrefs::size(), 1u);
    ASSERT_FALSE(step_until_set_frame().empty());

    // --- the in-memory state is changed, then restore_setpoints() brings the saved band back ---------------------
    hp().mode = CLIMATE_MODE_COOL;
    hp().target_temperature_low = 10.0f;
    hp().target_temperature_high = 12.0f;
    cn105_test::TestLog::clear();
    profile().restore_setpoints();
    EXPECT_EQ(hp().mode, CLIMATE_MODE_HEAT_COOL);
    EXPECT_FLOAT_EQ(hp().target_temperature_low, 20.0f);
    EXPECT_FLOAT_EQ(hp().target_temperature_high, 24.0f);
    EXPECT_TRUE(cn105_test::TestLog::contains("restored HEAT_COOL band low=20.0 high=24.0"));
    EXPECT_EQ(cn105_test::TestPrefs::size(), 1u) << "restoring does not write";

    // --- a record saved while in another mode is not a restorable band --------------------------------------------
    run_for(1000);
    hp().make_call().set_mode(CLIMATE_MODE_HEAT).perform();
    EXPECT_EQ(cn105_test::TestPrefs::size(), 1u) << "same slot, overwritten";
    hp().mode = CLIMATE_MODE_COOL;
    hp().target_temperature_low = 10.0f;
    hp().target_temperature_high = 12.0f;
    cn105_test::TestLog::clear();
    profile().restore_setpoints();
    EXPECT_TRUE(cn105_test::TestLog::contains("saved state is not a restorable HEAT_COOL band"));
    EXPECT_EQ(hp().mode, CLIMATE_MODE_COOL) << "nothing restored";
    EXPECT_FLOAT_EQ(hp().target_temperature_low, 10.0f);
    EXPECT_FLOAT_EQ(hp().target_temperature_high, 12.0f);
}
