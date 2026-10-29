/// test_lossnay_specifics.cpp — Characterization of what is specific to Lossnay mode (device type 0x34)
/// Role: pins, on the unmodified component built with lossnay = true, the entity-level restrictions and the
///       status decoding that plan 01-10 (ProfileStateLossnay) does not cover: the poll request list, the
///       actual-mode status (0x09) answered to its own request, the allowed diagnostic entities, the refusal
///       of run-state and functions commands (auxiliary controls) and the OFF transition (PR 1b, plan 01-14).
/// Deps: fixture.h, real_frames.h, cn105.h, lossnay_profile.h and the sensor headers of components/cn105
///
/// Frame rule: the only Lossnay captures of the repository are the 0x02 settings replies and the 0x09 actual-
/// mode replies (real_frames.h, from test_lossnay.cpp). No Lossnay handshake reply (0x7A with profile 0x34) and
/// no Lossnay 0x03 / 0x06 / 0x04 reply exists (tests/unit/README.md, Wanted captures): the connected state is
/// reached through the production handleConnectionSuccess() exactly as plan 01-10 does, and where a poll cycle
/// needs an answer to those three requests the real HEAT-PUMP captures of real_frames.h are fed as payload
/// carriers. They are not Lossnay captures: they pin how the Lossnay profile decodes those bytes, never what a
/// Lossnay unit sends.

#include "fixture.h"

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "error_code_sensor.h"
#include "input_power_sensor.h"
#include "kwh_sensor.h"
#include "outside_air_temperature_sensor.h"
#include "real_frames.h"
#include "runtime_hours_sensor.h"
#include "uptime_connection_sensor.h"

namespace {

using cn105_test::hex;
using cn105_test::TestLog;
namespace rf = cn105_test::real_frames;
using namespace esphome::climate;

std::string str(const char* s) { return s == nullptr ? std::string("<null>") : std::string(s); }

/// Heat-pump 0x06 reply quoted in the decode_status() comment of heatpump_profile.cpp (checksum 0xB7 verified in
/// test_entities.cpp): operating 1, input power 8 W, energy counter 1360 (136.0 kWh). A heat-pump capture used as
/// a payload carrier, see the file header.
const std::vector<uint8_t> HEAT_PUMP_STATUS_8W_136KWH = {
    0xFC, 0x62, 0x01, 0x30, 0x10, 0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x05, 0x50, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00};

}  // namespace

/// Lossnay configured as climate.py validate_lossnay_config allows it: modes AUTO, HEAT, FAN_ONLY, OFF, fans LOW,
/// MEDIUM, MIDDLE, HIGH, no swing; the only optional entities attached are those Lossnay mode still accepts
/// (input power, energy, runtime hours, outside air temperature, error code, uptime connection).
class LossnaySpecifics : public cn105_test::Cn105Fixture {
   protected:
    cn105_test::Cn105Options options() const override {
        cn105_test::Cn105Options o;
        o.lossnay = true;
        return o;
    }

    void before_setup(esphome::CN105Climate& hp) override {
        auto& traits = hp.config_traits();
        traits.set_supported_modes({CLIMATE_MODE_OFF, CLIMATE_MODE_AUTO, CLIMATE_MODE_HEAT, CLIMATE_MODE_FAN_ONLY});
        traits.set_supported_fan_modes({CLIMATE_FAN_LOW, CLIMATE_FAN_MEDIUM, CLIMATE_FAN_MIDDLE, CLIMATE_FAN_HIGH});
        traits.set_supported_swing_modes({});

        input_power_.configure_entity_("Input power", 0, 0);
        kwh_.configure_entity_("Energy", 0, 0);
        runtime_.configure_entity_("Runtime hours", 0, 0);
        outside_.configure_entity_("Outside air temperature", 0, 0);
        error_.configure_entity_("Error code", 0, 0);
        uptime_.configure_entity_("HP uptime connection", 0, 0);
        input_power_.add_on_state_callback([this](float v) { power_.push_back(v); });
        kwh_.add_on_state_callback([this](float v) { kwh_values_.push_back(v); });
        runtime_.add_on_state_callback([this](float v) { runtime_values_.push_back(v); });
        outside_.add_on_state_callback([this](float v) { outside_values_.push_back(v); });
        error_.add_on_state_callback([this](const std::string& v) { error_values_.push_back(v); });
        uptime_.add_on_state_callback([this](float v) { uptime_values_.push_back(v); });

        hp.set_input_power_sensor(&input_power_);
        hp.set_kwh_sensor(&kwh_);
        hp.set_runtime_hours_sensor(&runtime_);
        hp.set_outside_air_temperature_sensor(&outside_);
        hp.set_error_code_sensor(&error_);
        hp.set_hp_uptime_connection_sensor(&uptime_);
    }

    /// Waits for the CONNECT frame the component writes (returns it).
    std::vector<uint8_t> wait_for_connect_frame() {
        std::vector<uint8_t> written;
        for (uint32_t waited = 0; written.empty() && waited < 30000; waited += 10) {
            step(10);
            written = take_tx();
        }
        return written;
    }

    /// No Lossnay handshake capture yet: connected through the production transition, the same way as
    /// ProfileStateLossnay (test_profile_state.cpp). handleConnectionSuccess() is what a 0x7A reply with profile
    /// 0x34 triggers in processCommand(); the real heat-pump reply stamps lastResponseMs (and is rejected).
    void connect_without_handshake_capture() {
        ASSERT_FALSE(wait_for_connect_frame().empty());
        feed(rf::CONNECT_REPLY);
        step(10);
        ASSERT_FALSE(hp().isHeatpumpConnected()) << "rejected: profile byte 0x30";
        hp().handleConnectionSuccess();
        ASSERT_TRUE(hp().isHeatpumpConnected());
    }

    /// The five replies of a full Lossnay poll cycle: the two Lossnay captures answer their own requests, the
    /// heat-pump captures answer 0x03, 0x06 and 0x04 (see the file header).
    static std::map<uint8_t, std::vector<uint8_t>> full_cycle(const std::vector<uint8_t>& settings,
                                                              const std::vector<uint8_t>& actual_mode) {
        return {{0x02, settings},
                {0x03, rf::ROOM_TEMP_23C},
                {0x06, HEAT_PUMP_STATUS_8W_136KWH},
                {0x09, actual_mode},
                {0x04, rf::ERROR_INFO_0x80}};
    }

    /// Steps by 10 ms until a 0x41 frame is written (max `max_ms`); other frames are dropped.
    std::vector<uint8_t> step_until_set_frame(uint32_t max_ms = 2000) {
        for (uint32_t waited = 0; waited < max_ms; waited += 10) {
            step(10);
            for (auto& f : take_tx_frames()) {
                if (f.size() > 1 && f[1] == 0x41) return f;
            }
        }
        return {};
    }

    esphome::InputPowerSensor input_power_;
    esphome::kWhSensor kwh_;
    esphome::RuntimeHoursSensor runtime_;
    esphome::OutsideAirTemperatureSensor outside_;
    esphome::ErrorCodeSensor error_;
    esphome::cn105::HpUpTimeConnectionSensor uptime_;
    std::vector<float> power_, kwh_values_, runtime_values_, outside_values_, uptime_values_;
    std::vector<std::string> error_values_;
};

// ════════════════════════════════════════════════════════════════
// Request list and poll order
// ════════════════════════════════════════════════════════════════

TEST_F(LossnaySpecifics, Characterize_LossnayPollCycleOrder) {
    // The registered requests are the same list as for a heat pump; what differs is which of them may be sent.
    const auto& requests = hp().scheduler_.requests_;
    std::vector<int> codes;
    for (const auto& r : requests) codes.push_back(r.code);
    EXPECT_EQ(codes, (std::vector<int>{0x02, 0x03, 0x06, 0x09, 0x42, 0x04, 0x05, 0x20, 0x22}));

    for (const auto& r : requests) {
        SCOPED_TRACE(r.code);
        if (r.code == 0x05 || r.code == 0x20 || r.code == 0x22) {
            EXPECT_TRUE(r.disabled) << "timers and functions are never polled in Lossnay mode";
        } else {
            EXPECT_FALSE(r.disabled);
        }
    }
    // 0x42 (HVAC options) stays registered but its canSend refuses: no auxiliary controls in Lossnay mode.
    for (const auto& r : requests) {
        if (r.code == 0x42) {
            ASSERT_TRUE(static_cast<bool>(r.canSend));
            EXPECT_FALSE(r.canSend(hp()));
        }
    }
    EXPECT_TRUE(TestLog::contains("Registering function settings requests (0x20/0x22), disabled"));

    // A complete cycle: 0x02, 0x03, 0x06, 0x09, then 0x04. 0x42 is skipped, 0x05/0x20/0x22 are disabled.
    connect_without_handshake_capture();
    ASSERT_FALSE(HasFatalFailure());
    const auto requested = run_poll_cycle(full_cycle(rf::LOSSNAY_HEAT_RECOVERY, rf::LOSSNAY_ACTUAL_BYPASS));
    EXPECT_EQ(requested, (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04}));
    EXPECT_FALSE(hp().loopCycle.isCycleRunning());
}

// ════════════════════════════════════════════════════════════════
// Actual mode (0x09) answered to its own request
// ════════════════════════════════════════════════════════════════

TEST_F(LossnaySpecifics, Characterize_LossnayActualModeFromCapturedStatus) {
    connect_without_handshake_capture();
    ASSERT_FALSE(HasFatalFailure());

    // --- automatic operation, 0x09 = actual heat recovery (real capture): HEATING ---------------------------------
    publishes_.clear();
    run_poll_cycle(full_cycle(rf::LOSSNAY_AUTOMATIC, rf::LOSSNAY_ACTUAL_HEAT_RECOVERY));
    EXPECT_EQ(hp().mode, CLIMATE_MODE_AUTO);
    EXPECT_EQ(hp().action, CLIMATE_ACTION_HEATING);
    ASSERT_FALSE(publishes_.empty());
    EXPECT_EQ(publishes_.back().mode, CLIMATE_MODE_AUTO);
    EXPECT_EQ(publishes_.back().action, CLIMATE_ACTION_HEATING);

    // --- same settings, 0x09 = actual bypass (real capture): FAN ------------------------------------------------------
    publishes_.clear();
    run_poll_cycle(full_cycle(rf::LOSSNAY_AUTOMATIC, rf::LOSSNAY_ACTUAL_BYPASS));
    EXPECT_EQ(hp().action, CLIMATE_ACTION_FAN);
    ASSERT_FALSE(publishes_.empty());
    EXPECT_EQ(publishes_.back().action, CLIMATE_ACTION_FAN);

    // --- the same 0x09 again changes nothing ------------------------------------------------------------------------
    publishes_.clear();
    run_poll_cycle(full_cycle(rf::LOSSNAY_AUTOMATIC, rf::LOSSNAY_ACTUAL_BYPASS));
    EXPECT_EQ(hp().action, CLIMATE_ACTION_FAN);
    for (const auto& p : publishes_) EXPECT_EQ(p.action, CLIMATE_ACTION_FAN);

    // --- the Lossnay profile has no stage / sub-mode sensors: the 0x09 payload only feeds the action --------------
    EXPECT_EQ(hp().stage_sensor_, nullptr);
    EXPECT_EQ(hp().Sub_mode_sensor_, nullptr);
    EXPECT_EQ(str(hp().currentSettings.stage), "<null>") << "the heat-pump stage decode is not run";
}

TEST_F(LossnaySpecifics, Characterize_LossnayOffFrameDecoded) {
    connect_without_handshake_capture();
    ASSERT_FALSE(HasFatalFailure());

    run_poll_cycle(full_cycle(rf::LOSSNAY_HEAT_RECOVERY, rf::LOSSNAY_ACTUAL_HEAT_RECOVERY));
    ASSERT_EQ(hp().mode, CLIMATE_MODE_HEAT);
    ASSERT_EQ(hp().action, CLIMATE_ACTION_HEATING);

    // The OFF capture (power byte 0): the climate publishes OFF / OFF, the mode stays "FAN" in the settings.
    publishes_.clear();
    run_poll_cycle(full_cycle(rf::LOSSNAY_OFF, rf::LOSSNAY_ACTUAL_HEAT_RECOVERY));
    EXPECT_EQ(hp().mode, CLIMATE_MODE_OFF);
    EXPECT_EQ(hp().action, CLIMATE_ACTION_OFF);
    EXPECT_EQ(str(hp().currentSettings.power), "OFF");
    ASSERT_FALSE(publishes_.empty());
    EXPECT_EQ(publishes_.back().mode, CLIMATE_MODE_OFF);
    EXPECT_EQ(publishes_.back().action, CLIMATE_ACTION_OFF);

    // Back on: the HEAT capture publishes HEAT / HEATING again.
    publishes_.clear();
    run_poll_cycle(full_cycle(rf::LOSSNAY_HEAT_RECOVERY, rf::LOSSNAY_ACTUAL_HEAT_RECOVERY));
    ASSERT_FALSE(publishes_.empty());
    EXPECT_EQ(publishes_.back().mode, CLIMATE_MODE_HEAT);
    EXPECT_EQ(publishes_.back().action, CLIMATE_ACTION_HEATING);
}

// ════════════════════════════════════════════════════════════════
// Allowed entities
// ════════════════════════════════════════════════════════════════

TEST_F(LossnaySpecifics, Characterize_LossnayAllowedSensorsPublish) {
    EXPECT_TRUE(uptime_values_.empty());
    connect_without_handshake_capture();
    ASSERT_FALSE(HasFatalFailure());
    // The uptime connection sensor publishes 0 three times on the way to the connection: two stop() calls during
    // the bootstrap (UART setup, then the CONNECT write) and the start() of the connection.
    EXPECT_EQ(uptime_values_, (std::vector<float>{0.0f, 0.0f, 0.0f}));

    run_poll_cycle(full_cycle(rf::LOSSNAY_HEAT_RECOVERY, rf::LOSSNAY_ACTUAL_BYPASS));

    // 0x03 reaches statusChanged() first (power and energy still NaN), 0x06 then publishes the Lossnay decode of
    // the carrier frame: input power 8 W and energy 136.0 kWh. Lossnay forces the compressor frequency to 0 and
    // operating to false.
    ASSERT_EQ(power_.size(), 2u);
    EXPECT_TRUE(std::isnan(power_[0]));
    EXPECT_FLOAT_EQ(power_[1], 8.0f);
    ASSERT_EQ(kwh_values_.size(), 2u);
    EXPECT_TRUE(std::isnan(kwh_values_[0]));
    EXPECT_FLOAT_EQ(kwh_values_[1], 136.0f);
    ASSERT_EQ(runtime_values_.size(), 2u);
    EXPECT_FLOAT_EQ(runtime_values_[1], 0.0f);
    ASSERT_EQ(outside_values_.size(), 2u);
    EXPECT_TRUE(std::isnan(outside_values_[1]));
    EXPECT_FALSE(hp().currentStatus.operating);
    EXPECT_FLOAT_EQ(hp().currentStatus.compressorFrequency, 0.0f);
    // 0x04 extended status 0x80 -> "No Error", via the shared error decoder.
    EXPECT_EQ(error_values_, (std::vector<std::string>{"No Error"}));
    EXPECT_FLOAT_EQ(hp().current_temperature, 23.0f);

    // Same replies again: the status is unchanged, nothing is republished (the error text always is).
    run_poll_cycle(full_cycle(rf::LOSSNAY_HEAT_RECOVERY, rf::LOSSNAY_ACTUAL_BYPASS));
    EXPECT_EQ(power_.size(), 2u);
    EXPECT_EQ(kwh_values_.size(), 2u);
    EXPECT_EQ(error_values_.size(), 2u);

    // Connection lost: the uptime sensor stops at 0 (silent link, ten update intervals).
    run_for(30000);
    ASSERT_FALSE(hp().isHeatpumpConnected());
    EXPECT_FLOAT_EQ(uptime_values_.back(), 0.0f);
}

// ════════════════════════════════════════════════════════════════
// Auxiliary controls refused
// ════════════════════════════════════════════════════════════════

TEST_F(LossnaySpecifics, Characterize_LossnayRunStatesRefused) {
    connect_without_handshake_capture();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle(full_cycle(rf::LOSSNAY_HEAT_RECOVERY, rf::LOSSNAY_ACTUAL_BYPASS));
    take_tx();

    // The wanted run states (what an air purifier switch would leave behind) are dropped by the profile:
    // allow_operation(AUXILIARY_CONTROLS) is false, so no packet is written and the wanted state is reset.
    hp().wantedRunStates.air_purifier = 1;
    hp().wantedRunStates.hasChanged = true;
    hp().wantedRunStates.hasBeenSent = false;
    TestLog::clear();
    EXPECT_TRUE(step_until_set_frame().empty()) << "no 0x41 run-states packet in Lossnay mode";
    EXPECT_TRUE(TestLog::contains("Ignoring unsupported Lossnay operation (3)"));
    EXPECT_FALSE(hp().wantedRunStates.hasChanged);
    EXPECT_EQ(hp().wantedRunStates.air_purifier, -1);

    // Direct call, as the climate reaches it from the loop: same outcome.
    hp().wantedRunStates.night_mode = 1;
    hp().wantedRunStates.hasChanged = true;
    TestLog::clear();
    hp().sendWantedRunStates();
    EXPECT_TRUE(take_tx().empty());
    EXPECT_TRUE(TestLog::contains("Ignoring unsupported Lossnay operation (3)"));
    EXPECT_EQ(hp().wantedRunStates.night_mode, -1);
}

TEST_F(LossnaySpecifics, Characterize_LossnayFunctionsCommandsRefused) {
    connect_without_handshake_capture();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle(full_cycle(rf::LOSSNAY_HEAT_RECOVERY, rf::LOSSNAY_ACTUAL_BYPASS));
    take_tx();

    // A set-functions request (installer buttons are not accepted by the Lossnay YAML schema, but the flag is
    // reachable): the loop consumes it, refuses it and clears the pending get as well.
    hp().isSetFunctions_ = true;
    hp().isGetFunctions_ = true;
    TestLog::clear();
    step(10);
    EXPECT_FALSE(hp().isSetFunctions_);
    EXPECT_FALSE(hp().isGetFunctions_);
    EXPECT_TRUE(TestLog::contains("Ignoring unsupported Lossnay operation (3)"));
    EXPECT_TRUE(take_tx().empty());

    // The direct entry points refuse too.
    EXPECT_FALSE(hp().setFunctions(hp().functions));
    hp().functionsArrived();
    EXPECT_TRUE(take_tx().empty());
}
