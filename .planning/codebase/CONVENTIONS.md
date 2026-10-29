---
last_mapped_commit: 5250ba782c261ae98fe54abd2475ae7ea7836475
last_mapped_at: 2026-10-27
---
# Coding Conventions

**Analysis Date:** 2026-10-27

## Naming Patterns

**Files:**
- C++ component sources and headers are snake_case or mixed-case, with no single rule: `components/cn105/request_scheduler.cpp`, `components/cn105/frame_parser.h`, `components/cn105/cn105_types.h`, but also `components/cn105/heatpumpFunctions.cpp`, `components/cn105/climateControls.cpp`, `components/cn105/Globals.h`. New files should use snake_case (the majority pattern in recent additions).
- Python integration files are lowercase: `components/cn105/climate.py`, `custom_components/mitsubishi_climate_proxy/config_flow.py`.
- Unit test files are `tests/unit/test_<topic>.cpp` (snake_case after `test_`), e.g. `tests/unit/test_setpoint_persistence.cpp`, `tests/unit/test_frame_parser.cpp`.
- Shared test stubs live under `tests/unit/mocks/` mirroring ESPHome include paths, e.g. `tests/unit/mocks/esphome/components/climate/climate_mode.h`.
- Per-sensor / per-entity headers in the component use the `<feature>_sensor.h` or `<feature>_select.h` pattern: `components/cn105/kwh_sensor.h`, `components/cn105/van_orientation_select.h`, `components/cn105/hvac_option_switch.h`.

**Classes and structs:**
- Main component: `CN105Climate` in `components/cn105/cn105.h`.
- ESPHome entity wrappers are PascalCase and inherit from both the ESPHome platform type and `Component`:
  ```cpp
  // components/cn105/compressor_frequency_sensor.h (pattern)
  class CompressorFrequencySensor : public sensor::Sensor, public Component { ... };
  ```
  Examples: `ISeeSensor`, `StageSensor`, `RuntimeHoursSensor`, `FunctionsButton`, `HardwareSettingSelect`.
- Protocol and pure logic classes are PascalCase: `FrameParser` (`components/cn105/frame_parser.h`), `RequestScheduler` (`components/cn105/request_scheduler.h`), `HeatPumpProtocol`, `LossnayProtocol` (`components/cn105/lossnay_profile_protocol.h`).
- Plain data records use lowercase-initial or snake-ish struct names inherited from the original code: `heatpumpSettings`, `wantedHeatpumpSettings`, `heatpumpStatus`, `heatpumpRunStates` in `components/cn105/cn105_types.h`, plus `InfoRequest` in `components/cn105/info_request.h`. Do not rename these; new data structs should prefer PascalCase.
- Enums are `enum class` with UPPER_SNAKE_CASE values: `DriverState::CONNECTED`, `VaneType::SPLIT_HORIZONTAL` (`components/cn105/cn105.h`), `ProfileFeature::REMOTE_TEMPERATURE` (`components/cn105/protocol_profile.h`).

**Functions and methods:**
- Mixed style. The dominant style in `CN105Climate` is camelCase: `setTargetTemperatureLow()`, `getTargetTemperatureHigh()`, `checkSum()`, `setupUART()`, `isHeatpumpConnected()`, `hasChanged()`, `setPowerSetting()`.
- ESPHome-facing setters and getters use snake_case, matching ESPHome's own API: `set_vertical_vane_select()`, `set_compressor_frequency_sensor()`, `get_compressor_frequency()`, `set_vane_type()`, `set_hardware_settings_interval()` (`components/cn105/cn105.h`).
- Newer modules (`RequestScheduler`, `FrameParser`, `HeatPumpProtocol`) use snake_case: `register_request()`, `send_request()`, `clear_requests()`, `frame_complete()`, `saved_setpoint_state()`.
- Rule for new code: match the surrounding class. Use snake_case in new protocol/helper code and in anything exposed through the ESPHome `set_*`/`get_*` API; keep camelCase only inside existing `CN105Climate` methods.

**Variables and members:**
- Private/protected members end with a trailing underscore: `current_request_index_`, `send_callback_`, `requests_`, `hp_uptime_connection_sensor_`, `remote_temp_margin_` (`components/cn105/request_scheduler.cpp`, `components/cn105/cn105.h`). `CN105Climate` has legacy members without the underscore (`autoUpdate`, `externalUpdate`, `wantedSettingsMutex`) — do not copy that style.
- Public member fields on `CN105Climate` also end in `_` (`iSee_sensor_`, `Functions_sensor_`). Mixed capitalization like `Functions_sensor_` is legacy.
- Null-initialize pointers at declaration: `sensor::Sensor* compressor_frequency_sensor_ = nullptr;`, or brace-init `text_sensor::TextSensor* stage_sensor_{ nullptr };`. Both forms appear; prefer `= nullptr`.
- Loop/index variables are `i`, `size_t i`, or `auto& req` for range loops over requests.

**Constants and macros:**
- Protocol constants are `static const` with UPPER_SNAKE_CASE in `components/cn105/cn105_types.h`: `CONNECT_LEN`, `CONNECT`, `HEADER`, `RQST_PKT_SETTINGS`, `RCVD_PKT_FAIL`, `DEFAULT_REMOTE_TEMP_KEEPALIVE_INTERVAL_MS`.
- Log tags are `static const char*` globals with `LOG_*_TAG` or `LOG_*` names, all defined in `components/cn105/cn105_types.h` (e.g. `LOG_CYCLE_TAG`, `LOG_CONN_TAG`, `LOG_HARDWARE_SELECT_TAG`). The main tag is `TAG = "CN105"`. Some files still use literal tags such as `ESP_LOGW("chkSum", ...)` and `ESP_LOGW("Decoder", ...)` in `components/cn105/hp_readings.cpp`; prefer a named constant in new code.
- Preprocessor `#define` is used only for two sizing values in `cn105_types.h` (`MAX_DATA_BYTES`, `MAX_DELAY_RESPONSE_FACTOR`). Prefer `static const` or `constexpr` for new constants.
- Python constants are `CONF_*` for YAML keys (`CONF_HORIZONTAL_VANES = "horizontal_vanes"`), `DEFAULT_*` for defaults, and `UPPER_SNAKE` for enumeration tuples (`TURN_ON_MODE_PRIORITY`, `VANE_TYPES`) in `components/cn105/climate.py`, `custom_components/mitsubishi_climate_proxy/climate.py`, and `custom_components/mitsubishi_climate_proxy/config_flow.py`.

**Namespaces:**
- C++ production code lives in `namespace esphome { namespace cn105 { ... } }` (see `components/cn105/cn105.h`, `components/cn105/localization.h`).
- Pure protocol helpers live in `namespace cn105_protocol` (`components/cn105/frame_parser.h`, `components/cn105/cn105_protocol.h`), and unit tests use `using namespace cn105_protocol;`.
- Do not add new `using namespace` at file scope in headers. `components/cn105/request_scheduler.cpp` uses `using namespace esphome;` in a `.cpp`, which is acceptable only in implementation files.

## Code Style

**Formatting:**
- No formatter config is tracked (no `.clang-format`, `.editorconfig`, or `.prettierrc` at repo root). Match the surrounding file.
- Indentation is 4 spaces in C++ (`components/cn105/request_scheduler.cpp`, `components/cn105/frame_parser.h`) and in the `.h` headers, with 2-space indent in some legacy files. Python uses 4 spaces.
- Braces: K&R-style opening braces on the same line for functions and control flow are the norm (`void RequestScheduler::clear_requests() {`).
- Single-line guards are used: `if (req.code != code) continue;`, `if (req.disabled) { return; }`.
- Long function signatures are split one parameter per line (see the constructor in `components/cn105/request_scheduler.cpp`).

**Linting:**
- No C++ linter or static-analysis config is tracked in the repository.
- Python has no `ruff`, `flake8`, `pylint`, or `black` configuration tracked; no `pyproject.toml` is present.
- The only automated check is the CI workflow in `.github/workflows/build.yaml`, which runs GoogleTest and compiles example ESPHome configs.

**Comments and file headers:**
- Most C++ files open with a header comment in the form `/// <file>.h — <one-line role>` followed by `/// Deps:` and sometimes `/// Role:`. Examples: `tests/unit/test_checksum.cpp`, `tests/unit/test_dual_setpoint_recenter.cpp`, `components/cn105/frame_parser.h`.
- Python integration files use `## <file>.py — <description>` with `## Role:` and `## Deps:` lines (`components/cn105/climate.py`, `custom_components/mitsubishi_climate_proxy/climate.py`, `custom_components/mitsubishi_climate_proxy/config_flow.py`).
- Comments and documentation are written in both English and French. Existing French comments are not to be translated en masse. Write new comments in English.
- Section banners in tests use box-drawing rules: `// ════...` followed by a title (see `tests/unit/test_frame_parser.cpp`, `tests/unit/test_real_frames.cpp`).
- Issue numbers are referenced in comments when fixing regressions: `// Expose the existing polling-cycle state` in `components/cn105/cn105.h`, `(issue #733)` in `components/cn105/esphome_compat.h` and `custom_components/...`. Keep this for regression fixes.
- Doxygen-style `@class` / `@brief` appears in a few headers, e.g. `components/cn105/request_scheduler.h`. Optional.

## Import Organization

**C++ order (observed):**
1. Local component headers using quotes: `#include "Globals.h"`, `#include "cn105_protocol.h"`.
2. ESPHome headers using angle brackets or `esphome/...` paths: `#include <esphome/components/sensor/sensor.h>`, `#include "esphome/core/preferences.h"`.
3. Standard library headers: `#include <vector>`, `#include <map>`, `#include <mutex>` (guarded by `#ifdef USE_ESP32` in `components/cn105/cn105.h`).

`components/cn105/cn105.h` shows this order: local headers first, then ESPHome, then standard. Keep it.

**Header guards:** Use `#pragma once`. No `#ifndef` guards are used in `components/cn105/`.

**Python order:**
1. Standard library (`import logging`, `from typing import ...`).
2. Third-party / ESPHome / Home Assistant (`import esphome.codegen as cg`, `from homeassistant.components.climate import ...`).
3. Local relative imports (`from . import DOMAIN` in `custom_components/mitsubishi_climate_proxy/config_flow.py`).

Use `try/except ImportError` for optional HA symbols, e.g. `HVACAction` in `custom_components/mitsubishi_climate_proxy/climate.py`.

**Path aliases:** None. Includes use plain file names relative to `components/cn105/`, and the unit test CMake target adds `components/cn105` and `tests/unit/mocks` as include directories (`tests/unit/CMakeLists.txt`).

## Error Handling

**C++ (ESPHome firmware):**
- Errors are logged and execution returns early. No C++ `try`/`throw` usage appears in `components/cn105/`. Examples: `components/cn105/cn105.cpp` returns `false` from a check at around line 159, and `components/cn105/hp_readings.cpp` logs `ESP_LOGW(..., "KO -> checksum mismatch ...")` and drops the frame.
- ESP-IDF return codes are checked and logged with `esp_err_to_name()`: `ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(pin_err));` in `components/cn105/cn105.cpp`.
- Unknown or invalid inputs are logged as warnings and ignored, not asserted: `ESP_LOGW(LOG_HARDWARE_SELECT_TAG, "Code %d received unknown value: %d", ...)` in `components/cn105/hardware_setting_select.cpp`.
- Protocol parsing returns `bool` / `std::optional`-like results. `HeatPumpProtocol::restored_setpoint_state()` returns an optional-like value tested with `.has_value()` (`tests/unit/test_setpoint_persistence.cpp`).
- Overflow and bounds are guarded before indexing: `FrameParser::feed()` resets on `bytes_read_ >= MAX_DATA_BYTES` (`components/cn105/frame_parser.h`).
- Unused callback arguments are explicitly voided: `r_error_info.onResponse = [this](CN105Climate& self) { (void)self; ... };` in `components/cn105/cn105.cpp`.

**Python (Home Assistant integration):**
- Use `ServiceValidationError` for user-facing service failures (imported in `custom_components/mitsubishi_climate_proxy/climate.py`).
- Parse user input defensively and fall back to defaults on `(TypeError, ValueError)`: see `custom_components/mitsubishi_climate_proxy/climate.py` near lines 225 and 346.
- Use `_LOGGER.warning` for recoverable configuration issues and `_LOGGER.debug` for state tracing (e.g. `custom_components/mitsubishi_climate_proxy/climate.py` lines 635, 673, 1015).

## Logging

**Framework:** ESPHome logging macros `ESP_LOGV`, `ESP_LOGD`, `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE` (C++); Python `logging` with `_LOGGER = logging.getLogger(__name__)` (`custom_components/mitsubishi_climate_proxy/climate.py`).

**Usage counts in `components/cn105/` (approximate):** `ESP_LOGD` ~128, `ESP_LOGI` ~81, `ESP_LOGW` ~44, `ESP_LOGV` ~9.

**Patterns:**
- Pass the tag as a named constant from `components/cn105/cn105_types.h` (`LOG_CYCLE_TAG`, `LOG_SETTINGS_TAG`, `LOG_ACTION_EVT_TAG`).
- Use `ESP_LOGD` for per-frame or per-request traces (`components/cn105/request_scheduler.cpp`: `ESP_LOGD(tag, "Sending %s (0x%02X)", ...)`).
- Use `ESP_LOGI` for state transitions and user-visible decisions (`components/cn105/extraComponents.cpp`: `ESP_LOGI(TAG, "Fahrenheit compatibility mode: %s", ...)`).
- Use `ESP_LOGW` for unexpected protocol data, unknown values, or disabled-feature control attempts.
- Use `%02X` for command bytes and `%d` for codes; format with `%s` plus a ternary for booleans (`value ? "true" : "false"`).
- Do not log credentials or Wi-Fi/API secrets.

## Comments

**When to comment:**
- Explain protocol bytes and why a value is what it is, with a capture or source reference: `// Source: Live debug logs from atom-s3 ESP32 (2026-05-02 14:04-14:05)` (`tests/unit/test_real_frames.cpp`), `// Info request for settings (0x02): FC 42 01 30 10 02 ...` (`tests/unit/test_checksum.cpp`).
- Document regression history with the issue number and the bug mechanism, as in `tests/unit/test_setpoint_persistence.cpp` and `tests/unit/test_dual_setpoint_recenter.cpp` (issue #673).
- Mark deliberate compatibility shims (`components/cn105/esphome_compat.h`, `components/cn105/climate.py` `_esphome_version_tuple`).

**JSDoc/TSDoc (Doxygen):** Optional. Used in `components/cn105/request_scheduler.h` (`@class`, `@brief`, `@param`). Python uses docstrings only on helpers (`_esphome_version_tuple()` in `components/cn105/climate.py`).

## Function Design

**Size:** Core protocol handlers in `components/cn105/hp_readings.cpp`, `components/cn105/cn105.cpp`, and `components/cn105/climateControls.cpp` are long. Do not add new large methods to `CN105Climate`; put new logic in a helper class (pattern: `RequestScheduler`, `FrameParser`, `HeatPumpProtocol`) with its own tests.

**Parameters:**
- Pass protocol buffers as `uint8_t data[]` plus `int len` (`checkSum(uint8_t bytes[], int len)` in `components/cn105/heatpumpFunctions.cpp`).
- Use `const char*` for log/labels and `esphome::StringRef` overloads where ESPHome passes that type (`hasChanged` overloads in `components/cn105/cn105.h`).
- Callbacks are `std::function`-based typedefs (`SendCallback`, `TimeoutCallback`, `TerminateCallback`, `ContextCallback` in `components/cn105/request_scheduler.h`).

**Return Values:**
- Boolean predicates named `is*`, `has*`, `should*`: `isHeatpumpConnected()`, `hasChanged()`, `is_empty()`.
- Use `float NAN` for "no value" temperature fields (`components/cn105/cn105_types.h` struct defaults, `tests/unit/test_dual_setpoint_recenter.cpp` uses `NAN` for unset low/high).
- Use `static_cast<uint8_t>(...)` when comparing enum values to stored `uint8_t` fields (`tests/unit/test_setpoint_persistence.cpp`).

## Module Design

**Exports:**
- Public API is declared in the class header; implementation is in the matching `.cpp`. Header-only ESPHome entities are the exception: `components/cn105/*_sensor.h`, `*_select.h`, `*_switch.h`, `*_button.h`, `*_number.h` are header-only classes.
- Standalone pure modules (no ESPHome dependency) are kept testable: `components/cn105/frame_parser.h`, `components/cn105/cn105_protocol.h`, `components/cn105/heatpump_profile_protocol.h`, `components/cn105/lossnay_profile_protocol.h`. New protocol logic goes in these, not in `CN105Climate`.
- `CN105Climate` declares `friend class cn105::HeatPumpProfile;` and `friend class cn105::LossnayProfile;` (`components/cn105/cn105.h`). Do not add new friends; expose a method instead.

**Barrel Files:** None. `components/cn105/__init__.py` is the ESPHome component package entry and registers the platform; it is not a barrel. `components/cn105/cn105.h` includes all sub-entity headers directly.

**Profiles:** Hardware variants are expressed as subclasses of `Cn105ProtocolProfile` (`components/cn105/protocol_profile.h`) with `HeatPumpProfile` (`components/cn105/heatpump_profile.h`) and `LossnayProfile` (`components/cn105/lossnay_profile.h`) as `final` subclasses. Use `final` on concrete profile classes.

---

*Convention analysis: 2026-10-27*
