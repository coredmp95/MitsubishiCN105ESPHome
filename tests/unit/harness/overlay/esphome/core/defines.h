/// defines.h — hand-written replacement for the ESPHome codegen output (D-01)
/// Role: force-included into every harness translation unit (-include esphome/core/defines.h),
///       exactly like ESPHome's own precompiled header does; the copy shipped inside the
///       ESPHome tree is a static-analysis dummy and must not win on the include path.
/// Deps: esphome/core/macros.h
#pragma once
#include "esphome/core/macros.h"

#define ESPHOME_BOARD "unit-test"
#define ESPHOME_VARIANT "HOST"

// Harness log level (D-22). deferCycle() = 1500 ms is pinned under this level.
#ifndef ESPHOME_LOG_LEVEL
#define ESPHOME_LOG_LEVEL ESPHOME_LOG_LEVEL_VERY_VERBOSE
#endif
#define ESPHOME_LOGGER_TX_BUFFER_SIZE 512

#if defined(USE_ESP8266)
#define ESPHOME_THREAD_SINGLE
#else
#define ESPHOME_THREAD_MULTI_ATOMICS
#endif

// ESPHome's ESP32 codegen always emits USE_NATIVE_64BIT_TIME (components/esp32/__init__.py): the
// scheduler and esphome::millis_64() then read esp_timer_get_time(), as on the device. ESP8266
// keeps the Millis64Impl rollover tracking over esphome::millis().
#if defined(USE_ESP32)
#define USE_NATIVE_64BIT_TIME
#endif

#define USE_BINARY_SENSOR
#define USE_BUTTON
#define USE_CLIMATE
#define USE_NUMBER
#define USE_SELECT
#define USE_SENSOR
#define USE_SWITCH
#define USE_TEXT_SENSOR
#define USE_ENTITY_DEVICE_CLASS
#define USE_ENTITY_ICON
#define USE_ENTITY_UNIT_OF_MEASUREMENT

// The real firmware has Wi-Fi: enables the WAIT_WIFI branch (see overlay wifi_component.h).
#define USE_WIFI

#define ESPHOME_COMPONENT_COUNT 64
#define ESPHOME_ENTITY_BINARY_SENSOR_COUNT 4
#define ESPHOME_ENTITY_BUTTON_COUNT 4
#define ESPHOME_ENTITY_CLIMATE_COUNT 2
#define ESPHOME_ENTITY_NUMBER_COUNT 4
#define ESPHOME_ENTITY_SELECT_COUNT 8
#define ESPHOME_ENTITY_SENSOR_COUNT 16
#define ESPHOME_ENTITY_SWITCH_COUNT 8
#define ESPHOME_ENTITY_TEXT_SENSOR_COUNT 8
// ESPHOME_PROJECT_NAME is deliberately not defined (application.cpp would then need a version).
