// Harness shim: 1000 Hz ticks make Application::loop read xTaskGetTickCount() as milliseconds.
#pragma once
#define CONFIG_FREERTOS_HZ 1000
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 5
#define CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES 4
