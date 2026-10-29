/// fixture.h — reusable TEST_F fixture running a real CN105Climate on a fake UART and the test clock
/// Role: base of every characterization test (D-18). It is also the seam the Phase 2 simulator
///       (SIM-02) builds on: a deterministic CN105Climate, bytes in/out on a FakeUart, and time that
///       only moves when step() moves it. It also holds the frame helpers (with_checksum, split_frames,
///       hex), the recording of every Home Assistant publish (Published) and the handshake / poll-cycle
///       drivers (connect, run_poll_cycle). Downstream plans add test files only, never edit this one.
/// Deps: GoogleTest, cn105.h (unmodified production class), test_platform.h, real_frames.h,
///       cn105_protocol.h, frame_parser.h, real ESPHome 2026.10.0 core
#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "cn105.h"
#include "test_platform.h"

namespace cn105_test {

// ════ Frame helpers ═══════════════════════════════════════════════════════════════════════════
/// Climate state as Home Assistant sees it, captured by an on-state callback at every publish.
struct Published {
    uint32_t at_ms;  // TestClock::now() when the publish happened
    esphome::climate::ClimateMode mode;
    esphome::climate::ClimateAction action;
    float target_temperature;
    float target_temperature_low;
    float target_temperature_high;
    float current_temperature;
    esphome::optional<esphome::climate::ClimateFanMode> fan_mode;
    esphome::climate::ClimateSwingMode swing_mode;
};

/// Appends the CN105 checksum (cn105_protocol::checksum) to a frame that has none.
std::vector<uint8_t> with_checksum(std::vector<uint8_t> frame_without_checksum);

/// Splits a byte stream into the complete CN105 frames it contains (cn105_protocol::FrameParser);
/// each returned frame keeps its checksum byte. Bytes of an incomplete trailing frame are dropped.
std::vector<std::vector<uint8_t>> split_frames(const std::vector<uint8_t>& bytes);

/// "FC 5A 01 ..." rendering for assertion messages.
std::string hex(const std::vector<uint8_t>& bytes);

// ════ Fake UART ═══════════════════════════════════════════════════════════════════════════════
/// Captures every byte the component writes (tx) and serves scripted bytes to read (rx).
struct FakeUart : esphome::uart::UARTComponent {
    std::vector<uint8_t> tx;
    std::deque<uint8_t> rx;

    void write_array(const uint8_t* data, size_t len) override { tx.insert(tx.end(), data, data + len); }
    bool peek_byte(uint8_t* data) override {
        if (rx.empty()) return false;
        *data = rx.front();
        return true;
    }
    bool read_array(uint8_t* data, size_t len) override {
        if (rx.size() < len) return false;
        for (size_t i = 0; i < len; i++) {
            data[i] = rx.front();
            rx.pop_front();
        }
        return true;
    }
    size_t available() override { return rx.size(); }
    esphome::uart::UARTFlushResult flush() override {
        return esphome::uart::UARTFlushResult::UART_FLUSH_RESULT_ASSUMED_SUCCESS;
    }
    void check_logger_conflict() override {}
#if defined(USE_ESP32) || defined(USE_ESP8266)
    void load_settings(bool) override {}
#endif
};

// ════ Options ═════════════════════════════════════════════════════════════════════════════════
struct Cn105Options {
    bool lossnay = false;
    uint32_t update_interval_ms = 1000;
    uint64_t start_ms = 0;  // value of the test clock when the fixture starts
    bool installer_mode = false;
};

// ════ Clock fixture ═══════════════════════════════════════════════════════════════════════════
/// For struct-level tests (cycleManagement, RequestScheduler) that need no climate. Every test starts
/// from a fresh esphome::App and reset platform state (clock, log capture, preferences, Wi-Fi, IDF
/// recorder); the log capture is dumped when the test fails.
class ClockFixture : public ::testing::Test {
   protected:
    /// Test-clock value at SetUp (0 here, Cn105Options::start_ms in Cn105Fixture).
    virtual uint64_t clock_start_ms() const { return 0; }

    void SetUp() override;
    void TearDown() override;
};

// ════ CN105Climate fixture ════════════════════════════════════════════════════════════════════
class Cn105Fixture : public ClockFixture {
   protected:
    /// Override to change the construction options (Lossnay, update interval, start time...).
    virtual Cn105Options options() const { return {}; }
    /// Override to attach entities, extra traits or setters before hp.setup() runs.
    virtual void before_setup(esphome::CN105Climate& hp) { (void) hp; }

    uint64_t clock_start_ms() const override { return options().start_ms; }
    void SetUp() override;
    void TearDown() override;

    esphome::CN105Climate& hp() { return *hp_; }

    /// Advance the test clock by ms, run one component loop and fire the due ESPHome scheduler items.
    void step(uint32_t ms);

    /// Return and clear what the component wrote to the UART since the last call.
    std::vector<uint8_t> take_tx();

    /// Same, split into frames (checksum included).
    std::vector<std::vector<uint8_t>> take_tx_frames();

    /// step(tick_ms) repeatedly until total_ms of test time has elapsed.
    void run_for(uint32_t total_ms, uint32_t tick_ms = 10);

    /// Queue a heat pump frame on the UART rx: the CN105 checksum is appended.
    void feed(std::vector<uint8_t> frame_without_checksum);
    /// Queue raw bytes on the UART rx, checksum included (malformed or partial frames).
    void feed_raw(const std::vector<uint8_t>& bytes);

    /// Runs the real handshake: steps until CONNECT is written, asserts its bytes, feeds the real
    /// FC 7A reply, steps once and asserts isHeatpumpConnected().
    void connect();

    /// Answers the info requests of one poll cycle: each FC 42 frame the component writes is answered
    /// with responses[code] (no answer when absent). Returns the info codes requested, in order. Stops
    /// once the cycle that started has ended, or after max_ms of test time.
    std::vector<uint8_t> run_poll_cycle(const std::map<uint8_t, std::vector<uint8_t>>& responses = default_responses(),
                                        uint32_t max_ms = 5000);

    /// 0x02 SETTINGS_ON_HEAT_21_5, 0x03 ROOM_TEMP_23C, 0x06 STATUS_IDLE, 0x09 POWER_IDLE,
    /// 0x04 ERROR_INFO_0x80 (frames without checksum).
    static std::map<uint8_t, std::vector<uint8_t>> default_responses();

    /// Every publish since SetUp, oldest first.
    std::vector<Published> publishes_;

    FakeUart uart_;

   private:
    // Several CN105Climate members are uninitialised by its constructor (audit BUG-08): construct it
    // into zero-filled storage so characterization is deterministic.
    alignas(esphome::CN105Climate) unsigned char storage_[sizeof(esphome::CN105Climate)];
    esphome::CN105Climate* hp_ = nullptr;
};

}  // namespace cn105_test
