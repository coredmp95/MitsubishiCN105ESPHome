/// fixture.cpp — implementation of the characterization fixtures (see fixture.h)
/// Role: re-creates esphome::App per test, builds a CN105Climate the way the generated code does
///       (components/cn105/climate.py to_code) and drives it on the test clock.
/// Deps: fixture.h, GoogleTest, test_platform.h

#include "fixture.h"

#include <cstdio>
#include <cstring>

#include "cn105_protocol.h"
#include "frame_parser.h"
#include "real_frames.h"

namespace cn105_test {

// ════ Frame helpers ═══════════════════════════════════════════════════════════════════════════
std::vector<uint8_t> with_checksum(std::vector<uint8_t> frame_without_checksum) {
    frame_without_checksum.push_back(
        cn105_protocol::checksum(frame_without_checksum.data(), static_cast<int>(frame_without_checksum.size())));
    return frame_without_checksum;
}

std::vector<std::vector<uint8_t>> split_frames(const std::vector<uint8_t>& bytes) {
    std::vector<std::vector<uint8_t>> frames;
    cn105_protocol::FrameParser parser;
    for (uint8_t b : bytes) {
        parser.feed(b);
        if (parser.frame_complete()) {
            const uint8_t* raw = parser.raw();
            frames.emplace_back(raw, raw + parser.frame_size());
            parser.reset();
        }
    }
    return frames;
}

std::string hex(const std::vector<uint8_t>& bytes) {
    std::string out;
    char buf[4];
    for (size_t i = 0; i < bytes.size(); i++) {
        std::snprintf(buf, sizeof buf, "%02X", bytes[i]);
        if (i != 0) out += ' ';
        out += buf;
    }
    return out;
}

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
    // Record every HA publish. Registered before before_setup()/setup() so nothing is missed.
    hp_->add_on_state_callback([this](esphome::climate::Climate& c) {
        publishes_.push_back(Published{esphome::millis(), c.mode, c.action, c.target_temperature,
                                       c.target_temperature_low, c.target_temperature_high, c.current_temperature,
                                       c.fan_mode, c.swing_mode});
    });
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

std::vector<std::vector<uint8_t>> Cn105Fixture::take_tx_frames() { return split_frames(take_tx()); }

void Cn105Fixture::run_for(uint32_t total_ms, uint32_t tick_ms) {
    for (uint32_t elapsed = 0; elapsed < total_ms; elapsed += tick_ms) step(tick_ms);
}

void Cn105Fixture::feed(std::vector<uint8_t> frame_without_checksum) {
    feed_raw(with_checksum(std::move(frame_without_checksum)));
}

void Cn105Fixture::feed_raw(const std::vector<uint8_t>& bytes) { uart_.rx.insert(uart_.rx.end(), bytes.begin(), bytes.end()); }

void Cn105Fixture::connect() {
    // CONNECT (FC 5A 01 30 02 CA 01 A8) leaves after the 10 s grace delay; allow some margin.
    const std::vector<uint8_t> connect_frame = {0xFC, 0x5A, 0x01, 0x30, 0x02, 0xCA, 0x01, 0xA8};
    std::vector<uint8_t> written;
    for (uint32_t waited = 0; written.empty() && waited < 30000; waited += 10) {
        step(10);
        written = take_tx();
    }
    ASSERT_EQ(written, connect_frame) << "CONNECT not written as expected: " << hex(written);
    feed(real_frames::CONNECT_REPLY);
    step(10);
    ASSERT_TRUE(hp().isHeatpumpConnected()) << "the real FC 7A reply did not connect";
}

std::vector<uint8_t> Cn105Fixture::run_poll_cycle(const std::map<uint8_t, std::vector<uint8_t>>& responses,
                                                  uint32_t max_ms) {
    std::vector<uint8_t> requested;
    bool started = false;
    for (uint32_t elapsed = 0; elapsed < max_ms; elapsed += 10) {
        step(10);
        for (const auto& frame : take_tx_frames()) {
            // Info request: FC 42 01 30 10 <code> ...
            if (frame.size() < 6 || frame[1] != 0x42) continue;
            const uint8_t code = frame[5];
            requested.push_back(code);
            auto it = responses.find(code);
            if (it != responses.end()) feed(it->second);
        }
        if (hp().loopCycle.isCycleRunning()) started = true;
        if (started && !hp().loopCycle.isCycleRunning()) break;
    }
    return requested;
}

std::map<uint8_t, std::vector<uint8_t>> Cn105Fixture::default_responses() {
    return {{0x02, real_frames::SETTINGS_ON_HEAT_21_5},
            {0x03, real_frames::ROOM_TEMP_23C},
            {0x06, real_frames::STATUS_IDLE},
            {0x09, real_frames::POWER_IDLE},
            {0x04, real_frames::ERROR_INFO_0x80}};
}

}  // namespace cn105_test
