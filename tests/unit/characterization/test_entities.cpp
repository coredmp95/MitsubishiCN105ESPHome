/// test_entities.cpp — Characterization of the auxiliary ESPHome entities of CN105Climate
/// Role: pins, on the unmodified component, what every optional entity (sensors, text sensors, selects,
///       switches, buttons, numbers) publishes from a real poll cycle, and what the controls write to the
///       UART. Each fixture subclass attaches its entities in before_setup() with the production setters,
///       as components/cn105/climate.py to_code does, then drives the component through the real handshake
///       and poll cycle. Publishes are observed with add_on_state_callback (PR 1b, plan 01-14).
/// Deps: fixture.h, real_frames.h, cn105.h and the header-only entity classes of components/cn105

#include "fixture.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "auto_sub_mode_sensor.h"
#include "compressor_frequency_sensor.h"
#include "error_code_sensor.h"
#include "functions_sensor.h"
#include "input_power_sensor.h"
#include "isee_sensor.h"
#include "kwh_sensor.h"
#include "outside_air_temperature_sensor.h"
#include "real_frames.h"
#include "runtime_hours_sensor.h"
#include "stage_sensor.h"
#include "sub_mode_sensor.h"
#include "target_humidity_sensor.h"
#include "uptime_connection_sensor.h"

namespace {

using cn105_test::hex;
namespace rf = cn105_test::real_frames;

// ════ Frames not in real_frames.h ═════════════════════════════════════════════════════════════
// Two 0x06 status replies quoted in the decode_status() comment of components/cn105/heatpump_profile.cpp
// (MSZ-RW25VGHZ-SC1 / MUZ-RW25VGHZ-SC1 units, the byte layout "OP IP IP EU EU" is documented there).
// They are the only non-zero status captures of the repository. The test below proves their quoted
// checksums (0x3C and 0xB7) match, which is what makes them captures and not hand-made frames.

// "FC 62 01 30 10 06 00 00 1A 01 00 00 ... 3C": compressor at 0x1A = 26 Hz, operating byte 1, no power.
const std::vector<uint8_t> STATUS_RUNNING_26HZ = {
    0xFC, 0x62, 0x01, 0x30, 0x10, 0x06, 0x00, 0x00, 0x1A, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// "FC 62 01 30 10 06 00 00 00 01 00 08 05 50 00 00 42 00 ... B7": operating 1, input power 0x0008 = 8 W,
// energy counter 0x0550 = 1360 (= 136.0 kWh), compressor byte 0.
const std::vector<uint8_t> STATUS_8W_136KWH = {
    0xFC, 0x62, 0x01, 0x30, 0x10, 0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x05, 0x50, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00};

/// Records every value an entity publishes (the callbacks of sensor, text sensor and binary sensor differ
/// only by their argument type).
template <typename T>
struct Recorder {
    std::vector<T> values;
    std::function<void(T)> callback() {
        return [this](T v) { values.push_back(v); };
    }
};

/// Gives an entity the name production codegen gives it (log lines and publishes read it).
template <typename E>
void name_entity(E& entity, const char* name) {
    entity.configure_entity_(name, 0, 0);
}

}  // namespace

// ════════════════════════════════════════════════════════════════
// Diagnostic sensors: one real poll cycle to the entity publishes
// ════════════════════════════════════════════════════════════════

/// Every diagnostic sensor and text sensor attached through its production setter. The compressor sensor
/// is attached WITHOUT set_compressor_frequency_report_when_idle, as a YAML that omits the option.
class EntitiesSensors : public cn105_test::Cn105Fixture {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        name_entity(compressor_, "Compressor frequency");
        name_entity(input_power_, "Input power");
        name_entity(kwh_, "Energy");
        name_entity(runtime_, "Runtime hours");
        name_entity(outside_, "Outside air temperature");
        name_entity(humidity_, "Target humidity");
        name_entity(isee_, "iSee");
        name_entity(stage_, "Stage");
        name_entity(sub_mode_, "Sub mode");
        name_entity(auto_sub_mode_, "Auto sub mode");
        name_entity(error_, "Error code");

        compressor_.add_on_state_callback(compressor_rec_.callback());
        input_power_.add_on_state_callback(power_rec_.callback());
        kwh_.add_on_state_callback(kwh_rec_.callback());
        runtime_.add_on_state_callback(runtime_rec_.callback());
        outside_.add_on_state_callback(outside_rec_.callback());
        humidity_.add_on_state_callback(humidity_rec_.callback());
        isee_.add_on_state_callback(isee_rec_.callback());
        stage_.add_on_state_callback(stage_rec_.callback());
        sub_mode_.add_on_state_callback(sub_mode_rec_.callback());
        auto_sub_mode_.add_on_state_callback(auto_sub_mode_rec_.callback());
        error_.add_on_state_callback(error_rec_.callback());

        hp.set_compressor_frequency_sensor(&compressor_);
        hp.set_input_power_sensor(&input_power_);
        hp.set_kwh_sensor(&kwh_);
        hp.set_runtime_hours_sensor(&runtime_);
        hp.set_outside_air_temperature_sensor(&outside_);
        hp.set_target_humidity_sensor(&humidity_);
        hp.set_isee_sensor(&isee_);
        hp.set_stage_sensor(&stage_);
        hp.set_sub_mode_sensor(&sub_mode_);
        hp.set_auto_sub_mode_sensor(&auto_sub_mode_);
        hp.set_error_code_sensor(&error_);
    }

    /// One poll cycle where the replies of 0x06 and 0x09 are replaced.
    std::vector<uint8_t> cycle_with(const std::vector<uint8_t>& status, const std::vector<uint8_t>& power) {
        auto responses = default_responses();
        responses[0x06] = status;
        responses[0x09] = power;
        return run_poll_cycle(responses);
    }

    esphome::CompressorFrequencySensor compressor_;
    esphome::InputPowerSensor input_power_;
    esphome::kWhSensor kwh_;
    esphome::RuntimeHoursSensor runtime_;
    esphome::OutsideAirTemperatureSensor outside_;
    esphome::TargetHumiditySensor humidity_;
    esphome::ISeeSensor isee_;
    esphome::StageSensor stage_;
    esphome::SubModSensor sub_mode_;
    esphome::AutoSubModSensor auto_sub_mode_;
    esphome::ErrorCodeSensor error_;

    Recorder<float> compressor_rec_, power_rec_, kwh_rec_, runtime_rec_, outside_rec_, humidity_rec_;
    Recorder<bool> isee_rec_;
    Recorder<std::string> stage_rec_, sub_mode_rec_, auto_sub_mode_rec_, error_rec_;
};

TEST(EntitiesFrames, Characterize_QuotedStatusFramesHaveValidChecksums) {
    // The quoted comment ends with the checksum the unit sent; the component's own checksum must agree.
    EXPECT_EQ(cn105_test::with_checksum(STATUS_RUNNING_26HZ).back(), 0x3C);
    EXPECT_EQ(cn105_test::with_checksum(STATUS_8W_136KWH).back(), 0xB7);
}

TEST_F(EntitiesSensors, Characterize_PollCyclePublishesDiagnostics) {
    connect();
    ASSERT_FALSE(HasFatalFailure());

    // --- nothing is published before the first poll cycle ---------------------------------------------------------
    EXPECT_TRUE(compressor_rec_.values.empty());
    EXPECT_TRUE(error_rec_.values.empty());

    // --- cycle 1: default real replies (0x06 all zero, 0x09 idle, 0x04 extended status 0x80) -----------------------
    const auto requested = run_poll_cycle();
    EXPECT_EQ(requested, (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04}));

    // 0x02: iSee and target humidity are decoded from the settings frame; humidity byte 0 is not published.
    ASSERT_EQ(isee_rec_.values.size(), 1u);
    EXPECT_FALSE(isee_rec_.values[0]);
    EXPECT_TRUE(humidity_rec_.values.empty()) << "humidity byte 0x00: unsupported, nothing published";

    // The poll order is 0x02, 0x03, 0x06: the room-temperature reply (0x03) reaches statusChanged() first, while
    // compressor frequency, input power and energy are still NaN, and republishes every status sensor; the
    // status reply (0x06, all zero) then changes the status again and publishes a second time.
    ASSERT_EQ(compressor_rec_.values.size(), 2u);
    EXPECT_TRUE(std::isnan(compressor_rec_.values[0])) << "published by the 0x03 reply, before any 0x06";
    EXPECT_FLOAT_EQ(compressor_rec_.values[1], 0.0f);
    ASSERT_EQ(power_rec_.values.size(), 2u);
    EXPECT_TRUE(std::isnan(power_rec_.values[0]));
    EXPECT_FLOAT_EQ(power_rec_.values[1], 0.0f);
    ASSERT_EQ(kwh_rec_.values.size(), 2u);
    EXPECT_TRUE(std::isnan(kwh_rec_.values[0]));
    EXPECT_FLOAT_EQ(kwh_rec_.values[1], 0.0f);
    // Runtime hours come from the 0x03 reply (bytes 11-13, all zero here), so they are already 0 at the first publish.
    ASSERT_EQ(runtime_rec_.values.size(), 2u);
    EXPECT_FLOAT_EQ(runtime_rec_.values[0], 0.0f);
    EXPECT_FLOAT_EQ(runtime_rec_.values[1], 0.0f);
    // The room-temperature reply carries no outside temperature (byte 0): NaN is published, twice.
    ASSERT_EQ(outside_rec_.values.size(), 2u);
    EXPECT_TRUE(std::isnan(outside_rec_.values[0]));
    EXPECT_TRUE(std::isnan(outside_rec_.values[1]));

    // 0x09 idle: stage IDLE, sub-mode NORMAL, auto sub-mode AUTO_OFF, each published once.
    EXPECT_EQ(stage_rec_.values, (std::vector<std::string>{"IDLE"}));
    EXPECT_EQ(sub_mode_rec_.values, (std::vector<std::string>{"NORMAL"}));
    EXPECT_EQ(auto_sub_mode_rec_.values, (std::vector<std::string>{"AUTO_OFF"}));
    // 0x04 extended status 0x80: bit 7 only announces error reporting -> "No Error".
    EXPECT_EQ(error_rec_.values, (std::vector<std::string>{"No Error"}));

    EXPECT_EQ(compressor_.state, 0.0f);
    EXPECT_EQ(stage_.state, "IDLE");
    EXPECT_EQ(hp().currentStatus.roomTemperature, 23.0f);

    // --- cycle 2: the compressor runs at 26 Hz (quoted capture) and the stage is LOW (real POWER_LOW) -------------
    cycle_with(STATUS_RUNNING_26HZ, rf::POWER_LOW);
    ASSERT_EQ(compressor_rec_.values.size(), 3u);
    EXPECT_FLOAT_EQ(compressor_rec_.values[2], 26.0f);
    // statusChanged() republishes every status sensor, changed or not (the 0x03 reply did not change anything).
    ASSERT_EQ(power_rec_.values.size(), 3u);
    ASSERT_EQ(kwh_rec_.values.size(), 3u);
    ASSERT_EQ(runtime_rec_.values.size(), 3u);
    ASSERT_EQ(outside_rec_.values.size(), 3u);
    EXPECT_EQ(stage_rec_.values, (std::vector<std::string>{"IDLE", "LOW"}));
    EXPECT_EQ(sub_mode_rec_.values.size(), 1u) << "sub-mode unchanged: not republished";
    EXPECT_EQ(auto_sub_mode_rec_.values.size(), 1u);
    EXPECT_EQ(error_rec_.values.size(), 2u) << "the error sensor publishes at every cycle, changed or not";
    EXPECT_EQ(isee_rec_.values.size(), 1u) << "iSee is offered at every settings frame; the binary sensor drops a repeat";

    // --- cycle 3: input power 8 W and energy 136.0 kWh (quoted capture) -------------------------------------------
    cycle_with(STATUS_8W_136KWH, rf::POWER_LOW);
    ASSERT_EQ(power_rec_.values.size(), 4u);
    EXPECT_FLOAT_EQ(power_rec_.values[3], 8.0f);
    ASSERT_EQ(kwh_rec_.values.size(), 4u);
    EXPECT_FLOAT_EQ(kwh_rec_.values[3], 136.0f);
    ASSERT_EQ(compressor_rec_.values.size(), 4u);
    EXPECT_FLOAT_EQ(compressor_rec_.values[3], 0.0f);

    // --- cycle 4: same 0x06 frame again -> status unchanged, nothing republished ---------------------------------
    cycle_with(STATUS_8W_136KWH, rf::POWER_LOW);
    EXPECT_EQ(power_rec_.values.size(), 4u);
    EXPECT_EQ(kwh_rec_.values.size(), 4u);
    EXPECT_EQ(compressor_rec_.values.size(), 4u);
}

TEST_F(EntitiesSensors, Characterize_ReportWhenIdleWarningLoggedOnce) {
    // The compressor sensor is configured without the report_when_idle option: loop() logs a recommendation,
    // once the UART is ready, and only once.
    EXPECT_EQ(cn105_test::TestLog::count("Compressor frequency sensor configured in YAML without report_when_idle"), 0u)
        << "not before the UART is ready";
    connect();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle();
    run_poll_cycle();
    EXPECT_EQ(cn105_test::TestLog::count("Compressor frequency sensor configured in YAML without report_when_idle"), 1u);
}

/// Same sensors with report_when_idle given (the YAML option present, either value).
class EntitiesSensorsReportWhenIdle : public EntitiesSensors {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        EntitiesSensors::before_setup(hp);
        hp.set_compressor_frequency_report_when_idle(true);
    }
};

TEST_F(EntitiesSensorsReportWhenIdle, Characterize_ReportWhenIdleOptionSilencesWarning) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle();
    EXPECT_EQ(cn105_test::TestLog::count("without report_when_idle"), 0u);
    EXPECT_TRUE(hp().compressor_frequency_report_when_idle_);
    // With report_when_idle the 26 Hz byte is reported whether the operating byte says so or not.
    cycle_with(STATUS_RUNNING_26HZ, rf::POWER_IDLE);
    ASSERT_EQ(compressor_rec_.values.size(), 3u);
    EXPECT_FLOAT_EQ(compressor_rec_.values[2], 26.0f);
}

/// Humidity and iSee need a settings frame with the corresponding bytes: only the real settings frames of
/// real_frames.h are fed. None of them carries a target humidity, so that publish path is pinned only for its
/// "byte 0" branch above. The iSee bit (mode byte above 8) has no capture either.
TEST_F(EntitiesSensors, Characterize_SettingsFrameWithoutHumidityNorISee) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    auto responses = default_responses();
    responses[0x02] = rf::SETTINGS_FAN2_VANE_UP;
    run_poll_cycle(responses);
    ASSERT_EQ(isee_rec_.values.size(), 1u);
    EXPECT_FALSE(isee_rec_.values[0]);
    EXPECT_TRUE(humidity_rec_.values.empty());
    EXPECT_TRUE(std::isnan(humidity_.state));
}
