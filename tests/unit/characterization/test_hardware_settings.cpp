/// test_hardware_settings.cpp — Characterization of the installer functions (0x20/0x22) and their entities
/// Role: pins, on the unmodified component and with installer mode requested, how the functions get/set
///       buttons, the code/value numbers, the functions text sensor and the HardwareSettingSelect entities
///       reach the UART and Home Assistant: request scheduling of 0x20/0x22, the two function packets
///       (0x1F / 0x21) written by heatpumpFunctions.cpp, the published texts and the select behaviour
///       (PR 1b, plan 01-14).
/// Deps: fixture.h, real_frames.h, cn105.h, heatpumpFunctions.h and the entity headers of components/cn105
///
/// Frame rule: no 0x20 / 0x22 reply capture exists (tests/unit/README.md, Wanted captures; plan 01-12 decision
/// "existing-only"), so NO functions reply is fed to the component and the decode of those replies is not
/// pinned. Where a test needs "functions already read", the decoded state (hp().functions) is seeded directly
/// with bytes laid out from the documented encoding ((code - 100) << 2 | value): that is test data for the
/// encoder, not a frame presented as a capture.

#include "fixture.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "esphome/components/number/number.h"
#include "functions_sensor.h"
#include "real_frames.h"

namespace {

using cn105_test::hex;
namespace rf = cn105_test::real_frames;

/// Value the seeded functions give to a code (cycles 1, 2, 3 so every value shows up).
int seeded_value(int code) { return 1 + (code - 101) % 3; }

/// Test data: the 15 + 15 raw function bytes of a unit that supports codes 101..128, in the documented encoding.
void seed_functions(heatpumpFunctions& functions, uint8_t first[15], uint8_t second[15]) {
    for (int i = 0; i < 15; i++) {
        const int code = 101 + i;
        first[i] = static_cast<uint8_t>(((code - 100) << 2) | seeded_value(code));
    }
    for (int i = 0; i < 15; i++) {
        const int code = 116 + i;
        second[i] = code <= 128 ? static_cast<uint8_t>(((code - 100) << 2) | seeded_value(code)) : 0;
    }
    functions.setData1(first);
    functions.setData2(second);
}

}  // namespace

/// Installer mode requested in YAML, with the functions entities and one hardware setting select attached
/// through the production setters (climate.py to_code: functions_sensor, functions_get_button,
/// functions_set_button, functions_set_code, functions_set_value, hardware_settings).
class HardwareSettings : public cn105_test::Cn105Fixture {
   protected:
    cn105_test::Cn105Options options() const override {
        cn105_test::Cn105Options opts;
        opts.installer_mode = true;
        return opts;
    }

    virtual uint32_t hardware_interval_ms() const { return 86400000; }
    virtual bool with_selects() const { return true; }

    void before_setup(esphome::CN105Climate& hp) override {
        functions_sensor_.configure_entity_("Functions", 0, 0);
        get_button_.configure_entity_("Get functions", 0, 0);
        set_button_.configure_entity_("Set functions", 0, 0);
        code_.configure_entity_("Function code", 0, 0);
        value_.configure_entity_("Function value", 0, 0);
        select_.configure_entity_("Setting 101", 0, 0);
        // number.new_number(min_value, max_value, step) of the code generation
        code_.traits.set_min_value(100.0f);
        code_.traits.set_max_value(128.0f);
        code_.traits.set_step(1.0f);
        value_.traits.set_min_value(1.0f);
        value_.traits.set_max_value(3.0f);
        value_.traits.set_step(1.0f);
        // select.register_select(options=[sorted labels])
        labels_ = {"Normal", "Eco", "Boost"};
        esphome::cn105_set_select_options(select_.traits, labels_);
        select_.add_on_state_callback([this](size_t index) { select_states_.push_back(select_.option_at(index)); });
        functions_sensor_.add_on_state_callback([this](const std::string& s) { texts_.push_back(s); });

        hp.set_functions_sensor(&functions_sensor_);
        hp.set_functions_get_button(&get_button_);
        hp.set_functions_set_button(&set_button_);
        hp.set_functions_set_code(&code_);
        hp.set_functions_set_value(&value_);
        if (with_selects()) {
            hp.set_hardware_settings_interval(hardware_interval_ms());
            hp.add_hardware_setting(&select_);
        }
    }

    /// Installer-mode connection. The unit is the one real heat pump of the repository: it answers the
    /// installer CONNECT (0x5B) with the real standard reply (0x7A), which the profile accepts. A real 0x7B reply
    /// would be the genuine installer answer: it is a wanted capture and is not invented here.
    void connect_installer() {
        const std::vector<uint8_t> installer_connect = cn105_test::with_checksum({0xFC, 0x5B, 0x01, 0x30, 0x02, 0xCA, 0x01});
        std::vector<uint8_t> written;
        for (uint32_t waited = 0; written.empty() && waited < 30000; waited += 10) {
            step(10);
            written = take_tx();
        }
        ASSERT_EQ(written, installer_connect) << hex(written);
        feed(rf::CONNECT_REPLY);
        step(10);
        ASSERT_TRUE(hp().isHeatpumpConnected());
    }

    /// Steps for total_ms answering the standard info requests with the real replies (functions requests stay
    /// unanswered) and returns every info code requested, in order.
    std::vector<uint8_t> requested_over(uint32_t total_ms) {
        std::vector<uint8_t> requested;
        const auto responses = default_responses();
        for (uint32_t elapsed = 0; elapsed < total_ms; elapsed += 10) {
            step(10);
            for (const auto& frame : take_tx_frames()) {
                if (frame.size() < 6 || frame[1] != 0x42) continue;
                requested.push_back(frame[5]);
                auto it = responses.find(frame[5]);
                if (it != responses.end()) feed(it->second);
            }
        }
        return requested;
    }

    /// Every 0x41 frame the component writes over total_ms of idle stepping (the heat pump answers nothing).
    std::vector<std::vector<uint8_t>> set_frames_over(uint32_t total_ms) {
        std::vector<std::vector<uint8_t>> out;
        for (uint32_t elapsed = 0; elapsed < total_ms; elapsed += 10) {
            step(10);
            for (const auto& frame : take_tx_frames()) {
                if (frame.size() > 1 && frame[1] == 0x41) out.push_back(frame);
            }
        }
        return out;
    }

    esphome::FunctionsSensor functions_sensor_;
    esphome::FunctionsButton get_button_, set_button_;
    esphome::FunctionsNumber code_, value_;
    esphome::HardwareSettingSelect select_{101, {{1, "Normal"}, {2, "Eco"}, {3, "Boost"}}};
    std::vector<std::string> labels_;
    std::vector<std::string> select_states_;
    std::vector<std::string> texts_;
};

// ════════════════════════════════════════════════════════════════
// Request scheduling of 0x20 / 0x22
// ════════════════════════════════════════════════════════════════

TEST_F(HardwareSettings, Characterize_InstallerConnectThenFunctionsRequestsRegistered) {
    // With a hardware setting configured the 0x20/0x22 requests are registered enabled, with the YAML interval.
    EXPECT_TRUE(cn105_test::TestLog::contains("Registering function settings requests (0x20/0x22) with interval 86400000 ms"));
    EXPECT_TRUE(hp().installer_mode_effective_);
    connect_installer();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_TRUE(hp().installer_mode_effective_) << "a reply to 0x5B keeps the installer handshake";
    EXPECT_TRUE(cn105_test::TestLog::contains("connection success (User, 0x7A)"));
}

TEST_F(HardwareSettings, Characterize_FunctionsGetButtonSchedules0x20And0x22) {
    connect_installer();
    ASSERT_FALSE(HasFatalFailure());

    // First cycle: the interval timer of 0x20/0x22 has never run, so 0x20 leaves right after the standard
    // requests. No 0x20 reply capture exists, so it goes unanswered: the cycle ends by its 3 s timeout and 0x22
    // is left to the next cycle (an unanswered request ends the cycle; the next one starts with the standard
    // requests again).
    EXPECT_EQ(run_poll_cycle(), (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04, 0x20}));
    EXPECT_EQ(run_poll_cycle(), (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04, 0x22}));
    // Nothing more within the 24 h interval: the following cycles carry the standard requests only.
    EXPECT_EQ(run_poll_cycle(), (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04}));

    // Pressing "get": the sensor says so at once, and the next cycles request 0x20 then 0x22 despite the interval.
    texts_.clear();
    get_button_.press();
    EXPECT_EQ(texts_, (std::vector<std::string>{"Operation pending, please wait."}));
    EXPECT_TRUE(hp().isGetFunctions_);
    EXPECT_EQ(run_poll_cycle(), (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04, 0x20}));
    EXPECT_FALSE(hp().isGetFunctions_);
    EXPECT_EQ(run_poll_cycle(), (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04, 0x22}));
    EXPECT_EQ(run_poll_cycle(), (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04}));
}

TEST_F(HardwareSettings, Characterize_FunctionsRequestFramesAreInfoRequests) {
    connect_installer();
    ASSERT_FALSE(HasFatalFailure());
    std::vector<std::vector<uint8_t>> function_requests;
    const auto responses = default_responses();
    for (uint32_t elapsed = 0; elapsed < 8000; elapsed += 10) {
        step(10);
        for (const auto& frame : take_tx_frames()) {
            if (frame.size() > 5 && frame[1] == 0x42) {
                if (frame[5] == 0x20 || frame[5] == 0x22) function_requests.push_back(frame);
                auto it = responses.find(frame[5]);
                if (it != responses.end()) feed(it->second);
            }
        }
    }
    ASSERT_EQ(function_requests.size(), 2u);
    // FC 42 01 30 10 <code>, zero payload, checksum over the first 21 bytes.
    for (const auto& frame : function_requests) {
        ASSERT_EQ(frame.size(), 22u);
        EXPECT_EQ((std::vector<uint8_t>(frame.begin(), frame.begin() + 5)), (std::vector<uint8_t>{0xFC, 0x42, 0x01, 0x30, 0x10}));
        for (size_t i = 6; i < 21; i++) EXPECT_EQ(frame[i], 0x00);
        EXPECT_EQ(frame.back(), cn105_protocol::checksum(frame.data(), 21));
    }
    EXPECT_EQ(function_requests[0][5], 0x20);
    EXPECT_EQ(function_requests[1][5], 0x22);
}

/// Hardware settings every 5 s instead of every 24 h.
class HardwareSettingsShortInterval : public HardwareSettings {
   protected:
    uint32_t hardware_interval_ms() const override { return 5000; }
};

TEST_F(HardwareSettingsShortInterval, Characterize_HardwareSettingsIntervalSchedulesRequests) {
    EXPECT_TRUE(cn105_test::TestLog::contains("Registering function settings requests (0x20/0x22) with interval 5000 ms"));
    connect_installer();
    ASSERT_FALSE(HasFatalFailure());
    std::vector<uint32_t> times_0x20;
    const auto responses = default_responses();
    for (uint32_t elapsed = 0; elapsed < 40000; elapsed += 10) {
        step(10);
        for (const auto& frame : take_tx_frames()) {
            if (frame.size() > 5 && frame[1] == 0x42) {
                if (frame[5] == 0x20) times_0x20.push_back(cn105_test::TestClock::now());
                auto it = responses.find(frame[5]);
                if (it != responses.end()) feed(it->second);
            }
        }
    }
    // The request recurs: the gap between two consecutive 0x20 requests is at least the interval.
    ASSERT_GE(times_0x20.size(), 2u);
    for (size_t i = 1; i < times_0x20.size(); i++) {
        EXPECT_GE(times_0x20[i] - times_0x20[i - 1], 5000u) << "request " << i;
    }
}

/// No hardware setting select at all, but the get button: a request only on demand.
class HardwareSettingsNoSelects : public HardwareSettings {
   protected:
    bool with_selects() const override { return false; }
};

TEST_F(HardwareSettingsNoSelects, Characterize_FunctionsRequestsDisabledWithoutSelects) {
    EXPECT_TRUE(cn105_test::TestLog::contains("Registering function settings requests (0x20/0x22), disabled"));
    connect_installer();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(run_poll_cycle(), (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04}));
    EXPECT_EQ(run_poll_cycle(), (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04}));

    // The get button re-enables the requests on demand.
    get_button_.press();
    EXPECT_EQ(run_poll_cycle(), (std::vector<uint8_t>{0x02, 0x03, 0x06, 0x09, 0x04, 0x20}));
}

// ════════════════════════════════════════════════════════════════
// Set button, code and value numbers
// ════════════════════════════════════════════════════════════════

TEST_F(HardwareSettings, Characterize_FunctionsSetBeforeGetIsRefused) {
    connect_installer();
    ASSERT_FALSE(HasFatalFailure());
    requested_over(8000);
    texts_.clear();
    set_button_.press();
    EXPECT_EQ(texts_, (std::vector<std::string>{"Please get the functions first."}));
    EXPECT_FALSE(hp().isSetFunctions_);
    EXPECT_TRUE(set_frames_over(3000).empty());
}

TEST_F(HardwareSettings, Characterize_FunctionsNumbersStoreCodeAndValue) {
    EXPECT_EQ(hp().functions_code_, 0);
    code_.make_call().set_value(105).perform();
    value_.make_call().set_value(2).perform();
    EXPECT_EQ(hp().functions_code_, 105);
    EXPECT_EQ(hp().functions_value_, 2);
    // The number entities refuse a value outside the ranges the code generation gives them (100..128 and 1..3):
    // it is not clamped, the control callback is not called and the stored value is unchanged.
    code_.make_call().set_value(300).perform();
    EXPECT_EQ(hp().functions_code_, 105);
    value_.make_call().set_value(0).perform();
    EXPECT_EQ(hp().functions_value_, 2);
}

TEST_F(HardwareSettings, Characterize_FunctionsSetButtonWritesCodeAndValue) {
    connect_installer();
    ASSERT_FALSE(HasFatalFailure());
    requested_over(8000);
    take_tx();

    uint8_t first[15], second[15];
    seed_functions(hp().functions, first, second);  // test data, see the file header
    ASSERT_TRUE(hp().functions.isValid());

    code_.make_call().set_value(101).perform();
    value_.make_call().set_value(3).perform();
    texts_.clear();
    set_button_.press();
    EXPECT_EQ(texts_, (std::vector<std::string>{"Operation pending, please wait."}));
    EXPECT_TRUE(cn105_test::TestLog::contains("Setting code 101 to value 3"));

    // Code 101 takes value 3: raw byte 0 = (101 - 100) << 2 | 3 = 0x07.
    first[0] = 0x07;
    std::vector<uint8_t> expected1 = {0xFC, 0x41, 0x01, 0x30, 0x10, 0x1F};
    expected1.insert(expected1.end(), first, first + 15);
    std::vector<uint8_t> expected2 = {0xFC, 0x41, 0x01, 0x30, 0x10, 0x21};
    expected2.insert(expected2.end(), second, second + 15);

    const auto frames = set_frames_over(3000);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0], cn105_test::with_checksum(expected1)) << hex(frames[0]);
    EXPECT_EQ(frames[1], cn105_test::with_checksum(expected2)) << hex(frames[1]);
    EXPECT_FALSE(hp().isSetFunctions_);
}

TEST_F(HardwareSettings, Characterize_FunctionsSetRequestsFunctionsAgain) {
    connect_installer();
    ASSERT_FALSE(HasFatalFailure());
    run_poll_cycle();
    run_poll_cycle();
    take_tx();
    uint8_t first[15], second[15];
    seed_functions(hp().functions, first, second);
    ASSERT_FALSE(hp().loopCycle.isCycleRunning());

    // Step by step, outside a cycle: the set request is served by loop(), which then flags "get functions" so
    // that the UI is refreshed with the unit's own values.
    hp().isSetFunctions_ = true;
    step(10);
    EXPECT_FALSE(hp().isSetFunctions_);
    EXPECT_TRUE(hp().isGetFunctions_);
    const auto frames = take_tx_frames();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0][5], 0x1F);
    EXPECT_EQ(frames[1][5], 0x21);
    EXPECT_EQ(run_poll_cycle().back(), 0x20) << "the next cycle asks for the functions again";
}

// ════════════════════════════════════════════════════════════════
// Functions text sensor and HardwareSettingSelect
// ════════════════════════════════════════════════════════════════

TEST_F(HardwareSettings, Characterize_FunctionsArrivedPublishesTextAndSelects) {
    // functionsArrived() is what the 0x22 reply handler calls once both parts are decoded. The decoded state is
    // seeded (no functions capture), the publishing is what is pinned.
    uint8_t first[15], second[15];
    seed_functions(hp().functions, first, second);
    hp().functionsArrived();

    ASSERT_EQ(texts_.size(), 1u);
    // "<code>: <value> " for every supported code, in raw-byte order, values 1..3 only.
    std::string expected;
    for (int code = 101; code <= 128; code++) expected += std::to_string(code) + ": " + std::to_string(seeded_value(code)) + " ";
    EXPECT_EQ(texts_[0], expected);
    // Code 101 has seeded value 1 -> "Normal".
    EXPECT_EQ(select_states_, (std::vector<std::string>{"Normal"}));
    EXPECT_EQ(select_.current_option(), "Normal");

    // Same data again: the text is republished, the unchanged select is not.
    hp().functionsArrived();
    EXPECT_EQ(texts_.size(), 2u);
    EXPECT_EQ(select_states_.size(), 1u);
}

TEST_F(HardwareSettings, Characterize_HardwareSettingSelectWritesFunctions) {
    connect_installer();
    ASSERT_FALSE(HasFatalFailure());
    requested_over(8000);
    take_tx();
    uint8_t first[15], second[15];
    seed_functions(hp().functions, first, second);

    select_.make_call().set_option("Boost").perform();
    EXPECT_TRUE(cn105_test::TestLog::contains("Hardware setting change: Code 101 -> 3 (Boost)"));
    EXPECT_TRUE(hp().isSetFunctions_);
    EXPECT_EQ(hp().functions.getValue(101), 3) << "internal structure updated at once";
    EXPECT_TRUE(select_states_.empty()) << "no optimistic publish: the select follows the read-back";

    first[0] = 0x07;
    std::vector<uint8_t> expected1 = {0xFC, 0x41, 0x01, 0x30, 0x10, 0x1F};
    expected1.insert(expected1.end(), first, first + 15);
    const auto frames = set_frames_over(3000);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0], cn105_test::with_checksum(expected1)) << hex(frames[0]);
    EXPECT_EQ(frames[1][5], 0x21);
}

TEST_F(HardwareSettings, Characterize_HardwareSettingSelectBeforeGetWritesNothing) {
    connect_installer();
    ASSERT_FALSE(HasFatalFailure());
    requested_over(8000);
    take_tx();
    ASSERT_FALSE(hp().functions.isValid());
    select_.make_call().set_option("Eco").perform();
    EXPECT_TRUE(hp().isSetFunctions_) << "the request is queued even when the functions were never read";
    EXPECT_TRUE(set_frames_over(3000).empty()) << "encode_functions() refuses invalid functions: nothing is written";
    EXPECT_FALSE(hp().isSetFunctions_);
}

TEST_F(HardwareSettings, Characterize_HardwareSettingSelectUnknownValueLogged) {
    EXPECT_EQ(select_.get_code(), 101);
    EXPECT_TRUE(select_.is_available());
    // A value outside the mapping (the unit reports 0 or 4+): warning, no publish.
    cn105_test::TestLog::clear();
    select_.update_state_from_value(7);
    EXPECT_TRUE(cn105_test::TestLog::contains("Code 101 received unknown value: 7"));
    EXPECT_TRUE(select_states_.empty());
    // A known value publishes once; the same value again is logged as unchanged.
    select_.update_state_from_value(2);
    EXPECT_EQ(select_states_, (std::vector<std::string>{"Eco"}));
    select_.update_state_from_value(2);
    EXPECT_EQ(select_states_.size(), 1u);
}

TEST_F(HardwareSettings, Characterize_HardwareSettingSelectControlGuards) {
    // disabled (the profile declined a functions reply): control ignored with a warning
    select_.set_enabled(false);
    EXPECT_FALSE(select_.is_available());
    cn105_test::TestLog::clear();
    select_.control("Eco");
    EXPECT_TRUE(cn105_test::TestLog::contains("Setting 101 is disabled/failed, control ignored."));
    EXPECT_FALSE(hp().isSetFunctions_);
    select_.set_enabled(true);

    // a label outside the mapping
    cn105_test::TestLog::clear();
    select_.control("Turbo");
    EXPECT_TRUE(cn105_test::TestLog::contains("Code 101 control value not found in mapping: Turbo"));
    EXPECT_FALSE(hp().isSetFunctions_);

    // no callback registered (entity not attached to a climate)
    esphome::HardwareSettingSelect orphan(102, {{1, "On"}});
    cn105_test::TestLog::clear();
    orphan.control("On");
    EXPECT_TRUE(cn105_test::TestLog::contains("Code 102 no callback registered!"));
}
