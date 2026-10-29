/// fixture.cpp — implementation of the characterization fixtures (see fixture.h)
/// Role: re-creates esphome::App per test, builds a CN105Climate the way the generated code does
///       (components/cn105/climate.py to_code) and drives it on the test clock.
/// Deps: fixture.h, GoogleTest, test_platform.h

#include "fixture.h"

#include <cstring>

namespace cn105_test {

// ════ ClockFixture ════════════════════════════════════════════════════════════════════════════
void ClockFixture::SetUp() {
    reset_platform(clock_start_ms());
    // esphome::App is a global constructed only by generated code: re-create it for every test, or a
    // set_timeout() would run on stale scheduler items (and on a never-constructed App at first use).
    new (&esphome::App) esphome::Application();
}

void ClockFixture::TearDown() {
    if (HasFailure()) TestLog::dump();
    esphome::App.~Application();
}

// ════ Cn105Fixture ════════════════════════════════════════════════════════════════════════════
void Cn105Fixture::SetUp() {
    ClockFixture::SetUp();
    const Cn105Options opts = options();

    // UART settings the generated code applies (8E1, 2400 baud); without them setupUART() logs
    // "not configured as SERIAL_8E1" and never reaches CONNECTING.
    uart_.set_data_bits(8);
    uart_.set_parity(esphome::uart::UART_CONFIG_PARITY_EVEN);
    uart_.set_stop_bits(1);
    uart_.set_baud_rate(2400);

    std::memset(storage_, 0, sizeof storage_);
    hp_ = new (storage_) esphome::CN105Climate(&uart_, opts.lossnay);
    esphome::App.register_climate(hp_, "clim", 0, 0);
    hp_->set_update_interval(opts.update_interval_ms);

    // Traits as climate.py to_code configures them for a typical heat pump.
    auto& traits = hp_->config_traits();
    for (auto mode : {esphome::climate::CLIMATE_MODE_OFF, esphome::climate::CLIMATE_MODE_HEAT,
                      esphome::climate::CLIMATE_MODE_COOL, esphome::climate::CLIMATE_MODE_DRY,
                      esphome::climate::CLIMATE_MODE_FAN_ONLY, esphome::climate::CLIMATE_MODE_AUTO}) {
        traits.add_supported_mode(mode);
    }
    for (auto fan : {esphome::climate::CLIMATE_FAN_AUTO, esphome::climate::CLIMATE_FAN_LOW,
                     esphome::climate::CLIMATE_FAN_MEDIUM, esphome::climate::CLIMATE_FAN_HIGH}) {
        traits.add_supported_fan_mode(fan);
    }
    for (auto swing : {esphome::climate::CLIMATE_SWING_OFF, esphome::climate::CLIMATE_SWING_VERTICAL}) {
        traits.add_supported_swing_mode(swing);
    }

    if (opts.installer_mode) hp_->set_installer_mode(true);
    before_setup(*hp_);
    hp_->setup();
}

void Cn105Fixture::TearDown() {
    if (hp_ != nullptr) {
        hp_->~CN105Climate();
        hp_ = nullptr;
    }
    ClockFixture::TearDown();
}

void Cn105Fixture::step(uint32_t ms) {
    TestClock::advance(ms);
    hp_->loop();
    esphome::App.scheduler.call(esphome::millis());
}

std::vector<uint8_t> Cn105Fixture::take_tx() {
    std::vector<uint8_t> out;
    out.swap(uart_.tx);
    return out;
}

}  // namespace cn105_test
