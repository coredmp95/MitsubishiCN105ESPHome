/// version.h — pins the ESPHome version seen by components/cn105 (D-02)
/// Role: replaces the static-analysis dummy (2099.12.0) of the ESPHome tree so that the
///       ESPHOME_VERSION_CODE branches of esphome_compat.h select the 2026.10 code paths.
/// Deps: esphome/core/macros.h
#pragma once
#include "esphome/core/macros.h"
#define ESPHOME_VERSION "2026.10.0"
#define ESPHOME_VERSION_CODE VERSION_CODE(2026, 10, 0)
