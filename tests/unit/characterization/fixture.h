/// fixture.h — reusable TEST_F fixture running a real CN105Climate on a fake UART and the test clock
/// Role: base of every characterization test (D-18). It is also the seam the Phase 2 simulator
///       (SIM-02) builds on: a deterministic CN105Climate, bytes in/out on a FakeUart, and time that
///       only moves when step() moves it. This part holds the fixture base; plan 01-02 adds frame
///       helpers, publish recording and the poll-cycle driver on top of it.
/// Deps: GoogleTest, cn105.h (unmodified production class), test_platform.h, real ESPHome 2026.10.0 core
#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <new>
#include <vector>

#include "cn105.h"
#include "test_platform.h"

namespace cn105_test {

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

    FakeUart uart_;

   private:
    // Several CN105Climate members are uninitialised by its constructor (audit BUG-08): construct it
    // into zero-filled storage so characterization is deterministic.
    alignas(esphome::CN105Climate) unsigned char storage_[sizeof(esphome::CN105Climate)];
    esphome::CN105Climate* hp_ = nullptr;
};

}  // namespace cn105_test
