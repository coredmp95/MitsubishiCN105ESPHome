// Harness shim: one FreeRTOS tick is one test-clock millisecond (portTICK_PERIOD_MS 1, with
// CONFIG_FREERTOS_HZ 1000 in sdkconfig.h), so the inline esphome::delay(ms) advances ms.
#pragma once
#include <cstdint>
#include <cstddef>
typedef int BaseType_t; typedef unsigned int UBaseType_t; typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define portTICK_PERIOD_MS 1
#define configTICK_RATE_HZ 1000
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 4
#define pdMS_TO_TICKS(x) (x)
extern "C" { int xPortInIsrContext(void); void vPortYield(void); }
