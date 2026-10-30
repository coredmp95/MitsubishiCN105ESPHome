/// stubs_esp32.cpp — link-level replacement of the ESP32 platform layer for the host harness
/// Role: the ONLY definitions, in the ESP32 variant, of every time symbol ESPHome declares
///       out-of-line, all on cn105_test::TestClock (D-05):
///         esphome::millis()   -> TestClock::now()  (the one CUSTOM_MILLIS reaches via Globals.h)
///         xTaskGetTickCount() -> TestClock::now()  (Application::loop, CONFIG_FREERTOS_HZ 1000)
///         esp_timer_get_time()-> TestClock::now64() * 1000 (inline micros()/millis_64() and, because
///                                the overlay defines USE_NATIVE_64BIT_TIME, the scheduler)
///         vTaskDelay(ticks)   -> TestClock::advance(ticks) (inline esphome::delay(); never sleeps)
///       esphome::delay, micros and millis_64 are inline in esp32/hal.h and get no definition here.
///       Also: the remaining FreeRTOS / ESP-IDF / esphome::arch_* one-liners, the recorded
///       IDF UART/GPIO calls (feed cn105_test::IdfRecorder) and esphome::random_uint32()
///       (-> cn105_test::TestRandom::next(), see the comment at its definition).
/// Deps: test_platform.h, tests/unit/harness/shim_esp32 headers, esphome/core/hal.h

#include <cstdint>
#include <cstdlib>
#include <mutex>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_chip_info.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "test_platform.h"

using cn105_test::IdfRecorder;
using cn105_test::TestClock;

extern "C" {

// ── Clock hooks (one definition each, all on TestClock) ──
int64_t esp_timer_get_time(void) { return static_cast<int64_t>(TestClock::now64()) * 1000; }
TickType_t xTaskGetTickCount(void) { return static_cast<TickType_t>(TestClock::now()); }
void vTaskDelay(TickType_t ticks) { TestClock::advance(ticks); }  // portTICK_PERIOD_MS is 1

// ── FreeRTOS ──
int xPortInIsrContext(void) { return 0; }
void vPortYield(void) {}
BaseType_t xTaskNotifyGiveIndexed(TaskHandle_t, UBaseType_t) { return 1; }
uint32_t ulTaskNotifyTakeIndexed(UBaseType_t, BaseType_t, TickType_t) { return 0; }
void vTaskNotifyGiveIndexedFromISR(TaskHandle_t, UBaseType_t, BaseType_t *) {}
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return nullptr; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return new std::mutex(); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t) {
    static_cast<std::mutex *>(h)->lock();
    return 1;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
    static_cast<std::mutex *>(h)->unlock();
    return 1;
}
TaskHandle_t esphome_main_task_handle = nullptr;

// ── ESP-IDF system ──
size_t heap_caps_get_free_size(unsigned) { return 100000; }
size_t heap_caps_get_largest_free_block(unsigned) { return 50000; }
void esp_system_abort(const char *) { std::abort(); }
uint32_t esp_cpu_get_cycle_count(void) { return 0; }
int esp_task_wdt_reset(void) { return 0; }
void esp_chip_info(esp_chip_info_t *c) {
    c->revision = 0;
    c->cores = 1;
}
esp_err_t esp_ota_get_bootloader_description(const void *, esp_bootloader_desc_t *) { return 0; }
uint16_t esp_rom_crc16_le(uint16_t, const uint8_t *, uint32_t) { return 0; }
uint16_t esp_rom_crc16_be(uint16_t, const uint8_t *, uint32_t) { return 0; }
const char *esp_err_to_name(esp_err_t) { return "ESP_ERR"; }

// ── Recorded driver calls: pin force_low_level_uart_reinit (cn105.cpp), incl. port/pin fallbacks ──
esp_err_t gpio_reset_pin(gpio_num_t pin) {
    IdfRecorder::record("gpio_reset_pin", pin);
    return 0;
}
esp_err_t gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t mode) {
    IdfRecorder::record("gpio_set_pull_mode", pin, mode);
    return 0;
}
esp_err_t uart_param_config(uart_port_t port, const uart_config_t *config) {
    IdfRecorder::record("uart_param_config", port, config->baud_rate);
    return 0;
}
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int) {
    IdfRecorder::record("uart_set_pin", port, tx, rx, rts);
    return 0;
}
esp_err_t uart_set_mode(uart_port_t, uart_mode_t) { return 0; }
esp_err_t uart_wait_tx_done(uart_port_t, TickType_t) { return 0; }
esp_err_t uart_set_sclk(uart_port_t, uart_sclk_t) { return 0; }
esp_err_t uart_set_baudrate(uart_port_t, uint32_t) { return 0; }
esp_err_t uart_set_line_inverse(uart_port_t, uint32_t) { return 0; }
esp_err_t uart_set_hw_flow_ctrl(uart_port_t, uart_hw_flowcontrol_t, uint8_t) { return 0; }
esp_err_t uart_set_rx_timeout(uart_port_t, uint8_t) { return 0; }
esp_err_t uart_flush_input(uart_port_t) { return 0; }
esp_err_t uart_get_baudrate(uart_port_t, uint32_t *baud) {
    *baud = 2400;
    return 0;
}

}  // extern "C"

namespace esphome {

uint32_t millis() { return TestClock::now(); }

void arch_init() {}
uint32_t arch_get_cpu_freq_hz() { return 240000000; }
void arch_restart() { std::abort(); }
void get_mac_address_raw(uint8_t *mac) {
    for (int i = 0; i < 6; i++) mac[i] = static_cast<uint8_t>(i);
}
bool random_bytes(uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) data[i] = 4;
    return true;
}
// Replaces ESPHome's own random_uint32(): helpers.cpp is compiled in the ESP32 variant under the name
// cn105_upstream_random_uint32 (generated TU in harness/CMakeLists.txt), so this is the only definition
// the scheduler links against. The upstream one is a splitmix32 over a file-static state that no test can
// reset and that advances on every call: the start offset of an interval then depended on how many draws
// earlier tests of the same process had made.
uint32_t random_uint32() { return cn105_test::TestRandom::next(); }

}  // namespace esphome
