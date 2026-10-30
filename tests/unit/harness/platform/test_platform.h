/// test_platform.h — control API of the host test platform (clock, random, logs, preferences, Wi-Fi, IDF calls)
/// Role: lets a test observe and control everything the unmodified components/cn105 reaches
///       through ESPHome: time, random numbers, log lines, persistent preferences, the Wi-Fi component
///       and the low-level ESP-IDF UART/GPIO calls. Part of the contract reused by the Phase 2 simulator.
/// Deps: <cstdint>, <string>, <vector>. No ESPHome header: usable from any test translation unit.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cn105_test {

// ════ Clock (D-05) ════════════════════════════════════════════════════════════════════════════
// One 64-bit millisecond counter owned by test_platform.cpp. Every ESPHome time hook of the
// variant (see stubs_<variant>.cpp) reads or advances this same counter, so time only moves when
// a test moves it. now() is the 32-bit view the firmware sees: it wraps at 2^32.
struct TestClock {
    static void reset(uint64_t now_ms = 0);
    static void set(uint64_t now_ms);
    static void advance(uint64_t delta_ms);
    static uint64_t now64();
    static uint32_t now();
};

// ════ Logs ════════════════════════════════════════════════════════════════════════════════════
struct LogRecord {
    int level;            // ESPHOME_LOG_LEVEL_* value (1 = error ... 7 = very verbose)
    std::string tag;
    std::string message;  // fully formatted
};

struct TestLog {
    static void clear();
    static const std::vector<LogRecord>& records();
    static bool contains(const std::string& substr);
    static size_t count(const std::string& substr);
    static void dump();   // every record to stderr; the fixture calls it when a test fails
};

// ════ Preferences ═════════════════════════════════════════════════════════════════════════════
// In-memory replacement of global_preferences (the ESP32 / ESP8266 backends are not compiled).
struct TestPrefs {
    static void clear();
    static size_t size();
};

// ════ Wi-Fi ═══════════════════════════════════════════════════════════════════════════════════
// Controls esphome::wifi::global_wifi_component. After a reset it is nullptr (no Wi-Fi component).
struct TestWifi {
    static void install(bool connected);
    static void set_connected(bool connected);
    static void remove();
};

// ════ Random ══════════════════════════════════════════════════════════════════════════════════
// The one source behind esphome::random_uint32() in both variants. It is a constant, not a sequence: a
// generator that advances on every call makes a test depend on how many draws earlier tests made in the
// same process (the upstream ESP32 random_uint32() is a splitmix32 over a file-static state, seeded once
// per process, and ESPHome draws from it for the start offset of every set_interval()). A test must give
// the same result alone, in a full run and in any order, so the value only changes when a test calls set().
// ESPHome draws it for the start offset of an interval: offset = (value * min(interval / 2, 5000 ms)) >> 32,
// so kDefault gives an offset of 0 ms (the first tick is immediate, the next ones are exactly one interval
// apart) and set(0x80000000) gives half of the maximum (2500 ms for an interval of 10 s or more).
struct TestRandom {
    static constexpr uint32_t kDefault = 4;  // small and non-zero: offset 0 ms for every interval
    static void reset();                     // back to kDefault
    static void set(uint32_t value);         // what random_uint32() returns until the next reset
    static uint32_t next();                  // used by stubs_<variant>.cpp
};

// ════ Resets ══════════════════════════════════════════════════════════════════════════════════
// Resets clock, logs, preferences, Wi-Fi, the random value and (USE_ESP32) the IDF recorder in one call.
void reset_platform(uint64_t start_ms = 0);

#ifdef USE_ESP32
// ════ Recorded ESP-IDF calls (ESP32 variant only: the ESP8266 build has no IDF layer) ═════════
struct IdfCall {
    std::string fn;
    int a0;
    int a1;
    int a2;
    int a3;
};

struct IdfRecorder {
    static void clear();
    static const std::vector<IdfCall>& calls();
    static void record(const char* fn, int a0 = 0, int a1 = 0, int a2 = 0, int a3 = 0);  // used by stubs_esp32.cpp
};
#endif

}  // namespace cn105_test
