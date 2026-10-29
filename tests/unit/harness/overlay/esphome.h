/// esphome.h — hand-written aggregate header for the host harness (replaces ESPHome codegen output)
/// Role: satisfies `#include <esphome.h>` in components/cn105 (Globals.h, request_scheduler.cpp).
/// Deps: the real ESPHome 2026.10.0 headers fetched by tests/unit/harness/CMakeLists.txt
#pragma once
#include "esphome/core/macros.h"
#include "esphome/core/defines.h"
#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "esphome/components/button/button.h"
#include "esphome/components/climate/climate.h"
