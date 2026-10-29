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

// ════════════════════════════════════════════════════════════════
// Controls: vane and airflow selects, HVAC option switches (PR 1b, Task 2 of plan 01-14)
// ════════════════════════════════════════════════════════════════
// Frame rule: the 0x41 frames the controls make the component write are checked against the real captures of
// real_frames.h when one exists (the vertical vane); where none exists (wide vane, airflow, HVAC options), the
// expected bytes are laid out here from the documented packet format, and the test says so. No reply frame is
// invented: the 0x42 HVAC-options reply stays unpinned (see tests/unit/README.md, Wanted captures).

namespace {

/// Records the option a select publishes (the callback gets an index).
struct SelectRecorder {
    std::vector<std::string> values;
    void attach(esphome::select::Select& select) {
        select.add_on_state_callback([this, &select](size_t index) { values.push_back(select.option_at(index)); });
    }
};

/// Records the state a switch publishes.
struct SwitchRecorder {
    std::vector<bool> values;
    void attach(esphome::switch_::Switch& sw) {
        sw.add_on_state_callback([this](bool state) { values.push_back(state); });
    }
};

/// The 0x41 frames of a byte stream.
std::vector<std::vector<uint8_t>> set_frames_of(const std::vector<std::vector<uint8_t>>& frames) {
    std::vector<std::vector<uint8_t>> out;
    for (const auto& f : frames) {
        if (f.size() > 1 && f[1] == 0x41) out.push_back(f);
    }
    return out;
}

}  // namespace

/// Vertical vane select only.
class EntitiesVerticalVane : public cn105_test::Cn105Fixture {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        name_entity(select_, "Vertical vane");
        recorder_.attach(select_);
        hp.set_vertical_vane_select(&select_);
    }

    /// Connects and completes a poll cycle with the given real 0x02 reply.
    void connected_with_settings(const std::vector<uint8_t>& settings) {
        connect();
        if (HasFatalFailure()) return;
        auto responses = default_responses();
        responses[0x02] = settings;
        run_poll_cycle(responses);
        take_tx();
    }

    esphome::VaneOrientationSelect select_;
    SelectRecorder recorder_;
};

TEST_F(EntitiesVerticalVane, Characterize_SetterBuildsOptionsFromVaneMap) {
    // set_vertical_vane_select() fills the options from the SwiCago vane map, in this order.
    ASSERT_EQ(select_.size(), 7u);
    std::vector<std::string> options;
    for (size_t i = 0; i < select_.size(); i++) options.push_back(select_.option_at(i));
    EXPECT_EQ(options, (std::vector<std::string>{"AUTO", "\xE2\x86\x91\xE2\x86\x91", "\xE2\x86\x91", "\xE2\x80\x94", "\xE2\x86\x93",
                                                "\xE2\x86\x93\xE2\x86\x93", "SWING"}));
    EXPECT_FALSE(select_.has_state()) << "nothing is published before the first settings reply";
}

TEST_F(EntitiesVerticalVane, Characterize_VerticalVaneSelectSendsSet) {
    connected_with_settings(rf::SETTINGS_ON_HEAT_21_5);
    ASSERT_FALSE(HasFatalFailure());
    // The settings reply (vane byte 0) is published to the select: AUTO.
    EXPECT_EQ(recorder_.values, (std::vector<std::string>{"AUTO"}));

    // A user picks "up" in Home Assistant: SelectCall -> VaneOrientationSelect::control -> setVaneSetting.
    select_.make_call().set_option("\xE2\x86\x91").perform();
    EXPECT_TRUE(hp().wantedSettings.hasChanged);
    EXPECT_FALSE(hp().wantedSettings.hasBeenSent);
    EXPECT_EQ(recorder_.values.size(), 1u) << "the select is not published optimistically: only on read-back";

    run_for(2000);
    const auto frames = set_frames_of(take_tx_frames());
    ASSERT_EQ(frames.size(), 1u);
    // The same bytes as the real capture SET_VaneUp.
    EXPECT_EQ(frames[0], cn105_test::with_checksum(rf::SET_VANE_UP)) << hex(frames[0]);

    // The unit confirms on the next poll (real reply with the vane at position 2): the select follows.
    auto responses = default_responses();
    responses[0x02] = rf::SETTINGS_FAN2_VANE_UP;
    run_poll_cycle(responses);
    EXPECT_EQ(recorder_.values.back(), "\xE2\x86\x91");
    EXPECT_EQ(select_.current_option(), "\xE2\x86\x91");
}

TEST_F(EntitiesVerticalVane, Characterize_VerticalVaneSwingFromSelect) {
    connected_with_settings(rf::SETTINGS_FAN2_VANE_UP);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(recorder_.values, (std::vector<std::string>{"\xE2\x86\x91"}));

    select_.make_call().set_option("SWING").perform();
    run_for(2000);
    const auto frames = set_frames_of(take_tx_frames());
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], cn105_test::with_checksum(rf::SET_VANE_SWING)) << hex(frames[0]);
}

/// Horizontal vane select on a split-vane unit: the wide-vane byte is decoded and written only when the
/// traits advertise horizontal swing (climate.py does that for horizontal_vanes / vane_type).
class EntitiesHorizontalVane : public cn105_test::Cn105Fixture {
   protected:
    virtual std::vector<std::string> yaml_options() const { return {}; }
    void before_setup(esphome::CN105Climate& hp) override {
        name_entity(select_, "Horizontal vane");
        recorder_.attach(select_);
        hp.config_traits().add_supported_swing_mode(esphome::climate::CLIMATE_SWING_HORIZONTAL);
        hp.config_traits().add_supported_swing_mode(esphome::climate::CLIMATE_SWING_BOTH);
        hp.set_vane_type(esphome::CN105Climate::VaneType::SPLIT_HORIZONTAL);
        hp.set_horizontal_vane_select(&select_, yaml_options());
    }
    esphome::VaneOrientationSelect select_;
    SelectRecorder recorder_;
};

TEST_F(EntitiesHorizontalVane, Characterize_AllWideVaneOptionsByDefault) {
    ASSERT_EQ(select_.size(), 8u);
    EXPECT_EQ(std::string(select_.option_at(7)), "AIRFLOW CONTROL");
    EXPECT_EQ(std::string(select_.option_at(6)), "SWING");
}

TEST_F(EntitiesHorizontalVane, Characterize_WideVaneSelectSendsSet) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    auto responses = default_responses();
    responses[0x02] = rf::SETTINGS_FAN2_VANE_UP;  // wide-vane byte 0x03
    run_poll_cycle(responses);
    take_tx();
    ASSERT_EQ(recorder_.values.size(), 1u);
    const std::string initial = recorder_.values[0];
    EXPECT_EQ(initial, "|") << "wide-vane byte 0x03 decodes to the centre position";

    select_.make_call().set_option("\xE2\x86\x90").perform();  // left arrow
    EXPECT_TRUE(hp().wantedSettings.hasChanged);
    run_for(2000);
    const auto frames = set_frames_of(take_tx_frames());
    ASSERT_EQ(frames.size(), 1u);
    // No capture exists for a wide-vane SET (README, Wanted captures): the expected frame is the documented
    // layout: byte 5 = 0x01 (settings), wide-vane flag 0x01 in byte 7, wide-vane value in byte 18.
    ASSERT_EQ(frames[0].size(), 22u);
    EXPECT_EQ(frames[0][5], 0x01);
    EXPECT_EQ(frames[0][7], 0x01) << "wide-vane flag";
    EXPECT_EQ(frames[0][18], 0x02) << "left arrow (WIDEVANE_MAP[1]) = wide-vane byte 0x02";
    EXPECT_EQ(frames[0].back(), cn105_protocol::checksum(frames[0].data(), 21));

    // Read-back: the next reply moves the select.
    responses[0x02] = rf::SETTINGS_FAN2_VANE_UP;
    run_poll_cycle(responses);
}

/// Options given in YAML (horizontal_vanes_options): unknown entries are dropped, the order is kept.
class EntitiesHorizontalVaneOptions : public EntitiesHorizontalVane {
   protected:
    std::vector<std::string> yaml_options() const override { return {"\xE2\x86\x90", "bogus", "|", "SWING"}; }
};

TEST_F(EntitiesHorizontalVaneOptions, Characterize_YamlOptionsFilteredAgainstWideVaneMap) {
    ASSERT_EQ(select_.size(), 3u);
    EXPECT_EQ(std::string(select_.option_at(0)), "\xE2\x86\x90");
    EXPECT_EQ(std::string(select_.option_at(1)), "|");
    EXPECT_EQ(std::string(select_.option_at(2)), "SWING");
}

/// Airflow control select (units with i-See and the "AIRFLOW CONTROL" wide-vane position).
class EntitiesAirflow : public cn105_test::Cn105Fixture {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        name_entity(select_, "Airflow control");
        recorder_.attach(select_);
        hp.set_airflow_control_select(&select_);
    }
    esphome::VaneOrientationSelect select_;
    SelectRecorder recorder_;
};

TEST_F(EntitiesAirflow, Characterize_AirflowRefusedWhenWideVaneIsNotAirflowControl) {
    EXPECT_EQ(std::string(select_.option_at(0)), "EVEN");
    EXPECT_EQ(std::string(select_.option_at(2)), "DIRECT");
    connect();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle();
    take_tx();
    // Without the i-See airflow byte the read-back publishes EVEN.
    EXPECT_EQ(recorder_.values, (std::vector<std::string>{"EVEN"}));

    // The profile refuses the change (wide vane is not "AIRFLOW CONTROL"): nothing wanted, nothing written,
    // and the select is re-published with the current value so Home Assistant snaps back.
    select_.make_call().set_option("DIRECT").perform();
    EXPECT_FALSE(hp().wantedRunStates.hasChanged);
    EXPECT_EQ(recorder_.values, (std::vector<std::string>{"EVEN", "EVEN"}));
    run_for(2000);
    EXPECT_TRUE(set_frames_of(take_tx_frames()).empty());
}

TEST_F(EntitiesAirflow, Characterize_AirflowSelectWritesRunStatesWhenAllowed) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle();
    take_tx();
    // No settings capture of a unit in "AIRFLOW CONTROL" position exists: the decoded state is set directly,
    // which is all can_control_airflow() reads.
    hp().currentSettings.wideVane = WIDEVANE_MAP[7];
    ASSERT_TRUE(hp().profile_->can_control_airflow());

    select_.make_call().set_option("DIRECT").perform();
    EXPECT_TRUE(hp().wantedRunStates.hasChanged);
    run_for(2000);
    const auto frames = set_frames_of(take_tx_frames());
    ASSERT_EQ(frames.size(), 1u);
    ASSERT_EQ(frames[0].size(), 22u);
    // Documented run-state layout: byte 5 = 0x08, flag RUN_STATE_PACKET_1[4] = 0x20 in byte 6, value
    // AIRFLOW_CONTROL[2] = 0x02 in byte 11.
    EXPECT_EQ(frames[0][5], 0x08);
    EXPECT_EQ(frames[0][6], 0x20);
    EXPECT_EQ(frames[0][11], 0x02);
    EXPECT_EQ(frames[0].back(), cn105_protocol::checksum(frames[0].data(), 21));
    // The select is published optimistically with the wanted value.
    EXPECT_EQ(recorder_.values.back(), "DIRECT");
    EXPECT_FALSE(hp().wantedRunStates.hasChanged) << "wanted run states are consumed by the send";
}

/// The three HVAC option switches.
class EntitiesHvacOptions : public cn105_test::Cn105Fixture {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        name_entity(air_purifier_, "Air purifier");
        name_entity(night_mode_, "Night mode");
        name_entity(circulator_, "Circulator");
        air_purifier_rec_.attach(air_purifier_);
        night_mode_rec_.attach(night_mode_);
        circulator_rec_.attach(circulator_);
        hp.set_air_purifier_switch(&air_purifier_);
        hp.set_night_mode_switch(&night_mode_);
        hp.set_circulator_switch(&circulator_);
    }
    esphome::HVACOptionSwitch air_purifier_, night_mode_, circulator_;
    SwitchRecorder air_purifier_rec_, night_mode_rec_, circulator_rec_;
};

TEST_F(EntitiesHvacOptions, Characterize_HvacOptionSwitchesEnable0x42Request) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    // The 0x42 request joins the cycle between the power/standby (0x09) and the error info (0x04) requests. The
    // repository has no 0x42 reply capture, so it goes unanswered here; the cycle moves on to 0x04 and ends.
    const auto requested = run_poll_cycle();
    EXPECT_EQ(requested, (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x42, 0x04}));
    EXPECT_FALSE(hp().loopCycle.isCycleRunning());
}

TEST_F(EntitiesHvacOptions, Characterize_HvacOptionSwitchWritesRunStates) {
    connect();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle();
    take_tx();

    // No 0x42 capture and no run-state SET capture exist: expected bytes are the documented layout. Byte 5 =
    // 0x08, flag RUN_STATE_PACKET_2[n] in byte 7, value in bytes 17 (air purifier), 18 (night mode) and 19
    // (circulator).
    const struct {
        const char* label;
        esphome::HVACOptionSwitch* sw;
        SwitchRecorder* rec;
        uint8_t flag;
        size_t value_index;
    } cases[] = {
        {"air purifier", &air_purifier_, &air_purifier_rec_, 0x04, 17},
        {"night mode", &night_mode_, &night_mode_rec_, 0x08, 18},
        {"circulator", &circulator_, &circulator_rec_, 0x10, 19},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.label);
        EXPECT_FALSE(c.sw->state);
        c.sw->turn_on();
        EXPECT_TRUE(hp().wantedRunStates.hasChanged);
        run_for(2000);
        const auto frames = set_frames_of(take_tx_frames());
        ASSERT_EQ(frames.size(), 1u);
        ASSERT_EQ(frames[0].size(), 22u);
        EXPECT_EQ(frames[0][5], 0x08);
        EXPECT_EQ(frames[0][6], 0x00);
        EXPECT_EQ(frames[0][7], c.flag);
        EXPECT_EQ(frames[0][c.value_index], 0x01);
        EXPECT_EQ(frames[0].back(), cn105_protocol::checksum(frames[0].data(), 21));
        // The switch is published ON at once (optimistic), once.
        EXPECT_TRUE(c.sw->state);
        EXPECT_EQ(c.rec->values, (std::vector<bool>{true}));
    }
    // Turning one off writes the same flag with the value 0.
    air_purifier_.turn_off();
    run_for(2000);
    const auto off_frames = set_frames_of(take_tx_frames());
    ASSERT_EQ(off_frames.size(), 1u);
    EXPECT_EQ(off_frames[0][7], 0x04);
    EXPECT_EQ(off_frames[0][17], 0x00);
    EXPECT_FALSE(air_purifier_.state);
    EXPECT_EQ(air_purifier_rec_.values, (std::vector<bool>{true, false}));
}

// ════════════════════════════════════════════════════════════════
// Uptime connection sensor
// ════════════════════════════════════════════════════════════════

class EntitiesUptime : public cn105_test::Cn105Fixture {
   protected:
    void before_setup(esphome::CN105Climate& hp) override {
        name_entity(uptime_, "HP uptime connection");
        uptime_.add_on_state_callback([this](float v) { values_.push_back(v); });
        hp.set_hp_uptime_connection_sensor(&uptime_);
    }
    esphome::cn105::HpUpTimeConnectionSensor uptime_;
    std::vector<float> values_;
};

TEST_F(EntitiesUptime, Characterize_UptimeConnectionSensorPublishes) {
    EXPECT_TRUE(values_.empty());
    EXPECT_NE(uptime_.unique_id().find("-uptime-hp_connection"), std::string::npos);
    uptime_.dump_config();

    // The bootstrap (UART setup, CONNECT write) stops the sensor twice and the reply starts it: 0 is published
    // three times on the way to the connection.
    connect();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(values_, (std::vector<float>{0.0f, 0.0f, 0.0f}));
    const size_t after_connect = values_.size();
    EXPECT_FLOAT_EQ(values_.back(), 0.0f) << "start() publishes the elapsed time since the connection: 0 s";

    // The polling tick ESPHome delivers every update_interval: seconds since the connection.
    run_for(5000);
    uptime_.update();
    ASSERT_EQ(values_.size(), after_connect + 1);
    EXPECT_NEAR(values_.back(), 5.0f, 0.1f);

    // The heat pump goes silent: after ten update intervals the link is judged lost, reconnectUART() runs
    // and stop() publishes 0.
    run_for(30000);
    ASSERT_FALSE(hp().isHeatpumpConnected());
    EXPECT_FLOAT_EQ(values_.back(), 0.0f);
    uptime_.update();
    EXPECT_FLOAT_EQ(values_.back(), 0.0f) << "while disconnected the polling tick publishes 0";
}
