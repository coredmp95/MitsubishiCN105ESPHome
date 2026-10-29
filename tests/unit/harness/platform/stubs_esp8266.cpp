/// stubs_esp8266.cpp — link-level replacement of the ESP8266 platform layer for the host harness
/// Role: the ONLY definitions, in the ESP8266 variant, of every time symbol ESPHome declares
///       out-of-line, all on cn105_test::TestClock (D-05):
///         esphome::millis() -> TestClock::now()  (the one CUSTOM_MILLIS reaches via Globals.h, and
///                              the one esphome::millis_64() = Millis64Impl::compute(millis()) in
///                              esp8266/hal.h and the scheduler (no USE_NATIVE_64BIT_TIME) feed on:
///                              ESPHome's own rollover tracking therefore runs on the test clock)
///         esphome::delay(ms) -> TestClock::advance(ms)  (never sleeps)
///         ::millis()        -> TestClock::now()   (Arduino-core C symbol, same clock)
///         ::micros()        -> low 32 bits of TestClock::now64() * 1000 (feeds inline esphome::micros())
///       esphome::millis_64 is inline in esp8266/hal.h and gets no definition here; components/esp8266/*.cpp
///       is not compiled (its core.cpp would define esphome::millis() and esphome::delay() a second time).
///       Also: the remaining Arduino-core / SDK one-liners (yield, esp_schedule, esp_delay, wdt, cycle
///       counter, EspClass) and esphome::arch_* / helpers one-liners.
/// Deps: test_platform.h, tests/unit/harness/shim_esp8266 headers, esphome/core/hal.h

#include <cstdint>
#include <cstdlib>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "test_platform.h"

using cn105_test::TestClock;

EspClass ESP;

uint32_t EspClass::getFreeHeap() { return 40000; }
uint32_t EspClass::getMaxFreeBlockSize() { return 30000; }
uint32_t EspClass::getChipId() { return 1; }
void EspClass::restart() { std::abort(); }
uint8_t EspClass::getHeapFragmentation() { return 0; }

void esp_delay(unsigned long, bool (*)(void)) {}

extern "C" {

// ── Arduino-core C symbols declared by esphome/components/esp8266/hal.h ──
unsigned long millis(void) { return static_cast<unsigned long>(TestClock::now()); }
unsigned long micros(void) { return static_cast<unsigned long>(static_cast<uint32_t>(TestClock::now64() * 1000)); }
void yield(void) {}

// ── ESP8266 SDK / core one-liners ──
void esp_schedule(void) {}
void system_soft_wdt_feed(void) {}
uint32_t esp_get_cycle_count(void) { return 0; }

}  // extern "C"

namespace esphome {

uint32_t millis() { return TestClock::now(); }
void delay(uint32_t ms) { TestClock::advance(ms); }

void arch_restart() { std::abort(); }
void get_mac_address_raw(uint8_t *mac) {
    for (int i = 0; i < 6; i++) mac[i] = static_cast<uint8_t>(i);
}
uint32_t random_uint32() { return 4; }

}  // namespace esphome
