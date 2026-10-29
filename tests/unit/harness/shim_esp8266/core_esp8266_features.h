/// core_esp8266_features.h — host replacement of the ESP8266 Arduino core feature header
/// Role: F_CPU and the cycle counter declared by esphome/components/esp8266/hal.h.
/// Deps: <cstdint>; esp_get_cycle_count() is defined in platform/stubs_esp8266.cpp
#pragma once
#include <cstdint>
#define F_CPU 80000000L
extern "C" uint32_t esp_get_cycle_count(void);
