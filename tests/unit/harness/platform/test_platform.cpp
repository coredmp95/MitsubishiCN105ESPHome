/// test_platform.cpp — variant-independent half of the host test platform
/// Role: owns the one 64-bit test clock behind cn105_test::TestClock, the value behind
///       cn105_test::TestRandom, captures ESPHome log lines, keeps preferences in memory and provides
///       the Wi-Fi component stub. It defines NO platform time symbol (no esphome::millis / delay,
///       no ::millis / micros): those live in stubs_<variant>.cpp and read this clock, so each time
///       symbol has exactly one definition (D-05). Likewise esphome::random_uint32() is defined in
///       stubs_<variant>.cpp and reads TestRandom.
/// Deps: test_platform.h, ESPHome 2026.10.0 core headers (overlay + fetched tree)

#include "test_platform.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/components/wifi/wifi_component.h"

namespace cn105_test {

// ════ Clock ═══════════════════════════════════════════════════════════════════════════════════
namespace {
uint64_t g_now_ms = 0;
}

void TestClock::reset(uint64_t now_ms) { g_now_ms = now_ms; }
void TestClock::set(uint64_t now_ms) { g_now_ms = now_ms; }
void TestClock::advance(uint64_t delta_ms) { g_now_ms += delta_ms; }
uint64_t TestClock::now64() { return g_now_ms; }
uint32_t TestClock::now() { return static_cast<uint32_t>(g_now_ms); }

// ════ Logs ════════════════════════════════════════════════════════════════════════════════════
namespace {
std::vector<LogRecord>& log_store() {
    static std::vector<LogRecord> records;
    return records;
}
}  // namespace

void TestLog::clear() { log_store().clear(); }
const std::vector<LogRecord>& TestLog::records() { return log_store(); }

bool TestLog::contains(const std::string& substr) { return count(substr) > 0; }

size_t TestLog::count(const std::string& substr) {
    size_t n = 0;
    for (const LogRecord& record : log_store()) {
        if (record.message.find(substr) != std::string::npos) n++;
    }
    return n;
}

void TestLog::dump() {
    static const char* const kLevels[] = {"NONE", "ERROR", "WARN", "INFO", "CONFIG", "DEBUG", "VERBOSE", "VVERBOSE"};
    fprintf(stderr, "---- TestLog: %zu record(s) ----\n", log_store().size());
    for (const LogRecord& record : log_store()) {
        const char* level = (record.level >= 0 && record.level <= 7) ? kLevels[record.level] : "?";
        fprintf(stderr, "[%-8s][%s] %s\n", level, record.tag.c_str(), record.message.c_str());
    }
    fprintf(stderr, "---- end of TestLog ----\n");
}

// ════ Preferences ═════════════════════════════════════════════════════════════════════════════
namespace {
std::map<uint32_t, std::vector<uint8_t>>& pref_store() {
    static std::map<uint32_t, std::vector<uint8_t>> store;
    return store;
}
}  // namespace

void TestPrefs::clear() { pref_store().clear(); }
size_t TestPrefs::size() { return pref_store().size(); }

// ════ Wi-Fi ═══════════════════════════════════════════════════════════════════════════════════
namespace {
esphome::wifi::WiFiComponent g_wifi;  // the stub behind esphome::wifi::global_wifi_component
}

void TestWifi::install(bool connected) {
    g_wifi.connected_ = connected;
    esphome::wifi::global_wifi_component = &g_wifi;
}
void TestWifi::set_connected(bool connected) { g_wifi.connected_ = connected; }
void TestWifi::remove() {
    g_wifi.connected_ = false;
    esphome::wifi::global_wifi_component = nullptr;
}

// ════ Random ══════════════════════════════════════════════════════════════════════════════════
namespace {
uint32_t g_random_value = TestRandom::kDefault;
}

void TestRandom::reset() { g_random_value = kDefault; }
void TestRandom::set(uint32_t value) { g_random_value = value; }
uint32_t TestRandom::next() { return g_random_value; }

#ifdef USE_ESP32
// ════ Recorded IDF calls ══════════════════════════════════════════════════════════════════════
namespace {
std::vector<IdfCall>& idf_store() {
    static std::vector<IdfCall> calls;
    return calls;
}
}  // namespace

void IdfRecorder::clear() { idf_store().clear(); }
const std::vector<IdfCall>& IdfRecorder::calls() { return idf_store(); }
void IdfRecorder::record(const char* fn, int a0, int a1, int a2, int a3) {
    idf_store().push_back(IdfCall{fn, a0, a1, a2, a3});
}
#endif

// ════ Reset ═══════════════════════════════════════════════════════════════════════════════════
void reset_platform(uint64_t start_ms) {
    TestClock::reset(start_ms);
    TestLog::clear();
    TestPrefs::clear();
    TestWifi::remove();
    TestRandom::reset();
#ifdef USE_ESP32
    IdfRecorder::clear();
#endif
}

}  // namespace cn105_test

// ════ ESPHome logging entry points (core/log.cpp is not compiled) ═════════════════════════════
namespace esphome {

void esp_log_vprintf_(int level, const char *tag, int line, const char *format, va_list args) {
    (void) line;
    va_list copy;
    va_copy(copy, args);
    const int needed = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string message;
    if (needed > 0) {
        message.resize(static_cast<size_t>(needed) + 1);
        vsnprintf(&message[0], message.size(), format, args);
        message.resize(static_cast<size_t>(needed));
    }
    cn105_test::LogRecord record;
    record.level = level;
    record.tag = tag != nullptr ? tag : "";
    record.message = message;
    cn105_test::log_store().push_back(record);
}

void esp_log_printf_(int level, const char *tag, int line, const char *format, ...) {
    va_list args;
    va_start(args, format);
    esp_log_vprintf_(level, tag, line, format, args);
    va_end(args);
}

#ifdef USE_ESP8266
// ESPHOME_LOG_FORMAT() wraps literals in PSTR(), which is a plain const char* on the host, so this
// overload is only reached by code that passes an explicit F("...") string.
void esp_log_printf_(int level, const char *tag, int line, const __FlashStringHelper *format, ...) {
    va_list args;
    va_start(args, format);
    esp_log_vprintf_(level, tag, line, reinterpret_cast<const char *>(format), args);
    va_end(args);
}
#endif

#ifdef USE_ESP32
int esp_idf_log_vprintf_(const char *format, va_list args) {
    esp_log_vprintf_(ESPHOME_LOG_LEVEL_ERROR, "esp-idf", 0, format, args);
    return 0;
}
#endif

// ════ Preferences (ESP32 backend replaced by an in-memory store) ══════════════════════════════
#ifdef USE_ESP32
namespace esp32 {

namespace {
std::map<uint32_t, std::vector<uint8_t>> &store() { return cn105_test::pref_store(); }
std::vector<std::unique_ptr<ESP32PreferenceBackend>> &backends() {
    static std::vector<std::unique_ptr<ESP32PreferenceBackend>> b;
    return b;
}
}  // namespace

bool ESP32PreferenceBackend::save(const uint8_t *data, size_t len) {
    store()[this->key].assign(data, data + len);
    return true;
}

bool ESP32PreferenceBackend::load(uint8_t *data, size_t len) {
    auto it = store().find(this->key);
    if (it == store().end() || it->second.size() != len) return false;
    memcpy(data, it->second.data(), len);
    return true;
}

void ESP32Preferences::open() {}
ESPPreferenceObject ESP32Preferences::make_preference(size_t, uint32_t type, bool in_flash) {
    backends().emplace_back(new ESP32PreferenceBackend());
    backends().back()->key = type;
    backends().back()->in_flash = in_flash;
    return ESPPreferenceObject(backends().back().get());
}
ESPPreferenceObject ESP32Preferences::make_preference(size_t length, uint32_t type) {
    return this->make_preference(length, type, true);
}
bool ESP32Preferences::load_from_key(uint32_t type, uint8_t *data, size_t len) {
    auto it = store().find(type);
    if (it == store().end() || it->second.size() != len) return false;
    memcpy(data, it->second.data(), len);
    return true;
}
bool ESP32Preferences::sync() { return true; }
bool ESP32Preferences::reset() { return true; }

}  // namespace esp32

static esp32::ESP32Preferences g_prefs;
ESPPreferences *global_preferences = &g_prefs;
#endif  // USE_ESP32

// ════ Preferences (ESP8266 backend replaced by an in-memory store) ════════════════════════════
#ifdef USE_ESP8266
namespace esp8266 {

namespace {
std::map<uint32_t, std::vector<uint8_t>> &store() { return cn105_test::pref_store(); }
// Owned by a static pool so the raw pointer inside an ESPPreferenceObject stays valid across
// TestPrefs::clear() (same contract as the ESP32 variant).
std::vector<std::unique_ptr<ESP8266PreferenceBackend>> &backends() {
    static std::vector<std::unique_ptr<ESP8266PreferenceBackend>> b;
    return b;
}
}  // namespace

bool ESP8266PreferenceBackend::save(const uint8_t *data, size_t len) {
    store()[this->type].assign(data, data + len);
    return true;
}

bool ESP8266PreferenceBackend::load(uint8_t *data, size_t len) {
    auto it = store().find(this->type);
    if (it == store().end() || it->second.size() != len) return false;
    memcpy(data, it->second.data(), len);
    return true;
}

void ESP8266Preferences::setup() {}
ESPPreferenceObject ESP8266Preferences::make_preference(size_t, uint32_t type, bool in_flash) {
    backends().emplace_back(new ESP8266PreferenceBackend());
    backends().back()->type = type;
    backends().back()->in_flash = in_flash;
    return ESPPreferenceObject(backends().back().get());
}
bool ESP8266Preferences::sync() { return true; }
bool ESP8266Preferences::reset() { return true; }

}  // namespace esp8266

static esp8266::ESP8266Preferences g_prefs;
ESPPreferences *global_preferences = &g_prefs;
#endif  // USE_ESP8266

// ════ Wi-Fi stub ══════════════════════════════════════════════════════════════════════════════
namespace wifi {
WiFiComponent *global_wifi_component = nullptr;
}  // namespace wifi

}  // namespace esphome
