<!-- GSD:project-start source:PROJECT.md -->

## Project

**MitsubishiCN105ESPHome — Reliability & Architecture Milestone**

An ESPHome external component (`components/cn105/`) that controls Mitsubishi heat pumps and air conditioners over the CN105 connector (UART 8E1, 2400 baud), exposing a Home Assistant `climate` entity plus vanes, power/energy sensors, sub-modes, error codes, installer functions, Fahrenheit and Lossnay support. A companion HA integration (`custom_components/mitsubishi_climate_proxy/`) handles single/dual setpoint display. This milestone fixes the bugs found in the 2026-10-27 audit, then refactors the component's internals, without changing anything users configure.

**Core Value:** The heat pump link never silently stalls or lies: polling keeps running indefinitely, user commands are either applied or visibly reported as failed, and Home Assistant shows the heat pump's real state (or "unavailable"), never stale or optimistic values.

### Constraints

- **Compatibility**: No breaking changes to YAML keys or HA entities; any rename goes through a deprecation warning — existing users must upgrade transparently.
- **Tests**: All existing unit tests keep passing, and every fixed bug gets a new unit test — the test suite is the main safety net for protocol logic.
- **Coverage**: Coverage of `components/cn105/` never drops below the recorded baseline, and changed lines are tested — the user's assurance that nothing breaks.
- **CI**: All 7 CI ESPHome configs (ESP32 + ESP8266) must compile — ESP8266 RAM/flash limits and multi-framework support (ESP-IDF, Arduino) are real.
- **Testability**: New logic goes into pure functions (`*_protocol.h` style) testable without ESPHome — keeps it under GoogleTest.
- **Platform**: ESPHome ≥ 2025.7 must keep working via `esphome_compat.h` — supported user base.
- **Docs**: User-visible changes are documented in the README — per the audit's definition of done.
- **Delivery**: One PR per phase, each releasable on its own — keeps reviews small and lets fixes ship early.

<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->

## Technology Stack

## Languages

- C++ (C++17) - Core ESPHome external component implementing the CN105 heat pump protocol. Sources in `components/cn105/*.cpp` and headers in `components/cn105/*.h`. C++17 is set in `tests/unit/CMakeLists.txt` (`CMAKE_CXX_STANDARD 17`).
- Python 3 - ESPHome component code-generation and YAML validation (`components/cn105/__init__.py`, `components/cn105/climate.py`), and the Home Assistant custom integration (`custom_components/mitsubishi_climate_proxy/*.py`).
- YAML - ESPHome device configurations in `examples/**/*.yaml` (e.g. `examples/root-configs/hp-sejour.yaml`, `examples/m5stack-atoms3/atom-m5.yaml`, `examples/modular-hvac/hvac-sample.yaml`).
- JSON - Home Assistant integration metadata and translations: `custom_components/mitsubishi_climate_proxy/manifest.json`, `custom_components/mitsubishi_climate_proxy/strings.json`, `custom_components/mitsubishi_climate_proxy/icons.json`, `custom_components/mitsubishi_climate_proxy/translations/en.json`, `hacs.json`.
- CMake - Standalone C++ unit-test build in `tests/unit/CMakeLists.txt`.

## Runtime

- ESPHome framework (external component, loaded via `external_components:`). Minimum supported ESPHome is 2025.7 per `README.md` (line 45); 2025.11+ recommended. Compile-time version shims exist for 2025.7 through 2026.x (`components/cn105/esphome_compat.h`, `components/cn105/hardware_setting_select.cpp`, `components/cn105/climate_action.h`, `components/cn105/uptime_connection_sensor.cpp`).
- Microcontroller targets:
- Firmware frameworks: ESP-IDF (`framework: type: esp-idf`) is used in most examples; Arduino is also supported (`examples/root-configs/esp32-test.yaml` shows a commented `type: arduino` alternative; `README.md` line 5 states compatibility with Arduino and ESP-IDF).
- pip (implicit, via ESPHome). No `requirements.txt` or `pyproject.toml` exists in the repository.
- Lockfile: Not applicable (no Python or C++ lockfile).
- `ghcr.io/esphome/esphome:stable` container (`.github/workflows/build.yaml`).

## Frameworks

- ESPHome (external component framework) - Provides `esphome::Component`, `climate::Climate`, `uart::UARTDevice`, `sensor`, `text_sensor`, `binary_sensor`, `switch`, `select`, `number`, `button`, `uptime`, and `preferences`. Imported in Python as `esphome.codegen`, `esphome.config_validation`, and `esphome.components.*` (`components/cn105/climate.py`).
- Home Assistant custom integration API - `homeassistant.config_entries`, `homeassistant.components.climate`, `homeassistant.components.select`, `homeassistant.helpers.event`, `homeassistant.helpers.restore_state` (`custom_components/mitsubishi_climate_proxy/`). No version pinned; `README.md` for the proxy notes HA 2024.12+ is required for `swing_horizontal_mode`.
- GoogleTest v1.15.2 - Fetched automatically with CMake `FetchContent` from `https://github.com/google/googletest.git` (`tests/unit/CMakeLists.txt`).
- CTest - Test discovery via `gtest_discover_tests` (`tests/unit/CMakeLists.txt`).
- ESPHome stubs - Hand-written mocks under `tests/unit/mocks/` (`tests/unit/mocks/esphome_stubs.h`, `tests/unit/mocks/esphome/core/version.h` defaults `ESPHOME_VERSION_CODE` to 2026.3.0, `tests/unit/mocks/esphome/components/climate/climate_mode.h`).
- CMake >= 3.14 - Unit test build system (`tests/unit/CMakeLists.txt`).
- ESPHome CLI (`esphome compile <config>.yaml`) - Firmware compilation, used by CI (`.github/workflows/build.yaml`).
- GitHub Actions - CI (`.github/workflows/build.yaml`), using `actions/checkout@v5`.
- VS Code - Editor settings in `.vscode/settings.json` (C++ file associations for standard library headers).
- HACS - Distribution metadata for the Home Assistant integration (`hacs.json`, `render_readme: true`, `zip_release: false`, `content_in_root: false`).

## Key Dependencies

- `esphome/core/*` (`component.h`, `log.h`, `helpers.h`, `preferences.h`, `version.h`, `macros.h`) - Core runtime, logging, persistent storage. Used throughout `components/cn105/`.
- `esphome/components/uart/uart.h` - UART transport to the heat pump CN105 connector. Configured by `components/cn105/climate.py` with 8 data bits, even parity, 1 stop bit (lines 606-608); baud rate is set on the UART component, not on the climate component (`components/cn105/climate.py` line 498 rejects `baud_rate`).
- `esphome/components/climate/*` - `ClimateTraits`, `climate_mode.h`, `climate_traits.h`.
- `esphome/components/{sensor,text_sensor,select,uptime}/*` - Entity types exposed by the component.
- `components/cn105/heatpumpFunctions.cpp`, `components/cn105/heatpumpFunctions.h`, `components/cn105/Globals.h` - Adapted from the SwiCago HeatPump Arduino library (referenced in `README.md` line 39). Also credits geoffdavis/esphome-mitsubishiheatpump (`README.md` line 7).
- `components/cn105/*_profile*.{cpp,h}` - Heat pump / Lossnay model profiles (`heatpump_profile.cpp`, `lossnay_profile.cpp`, `protocol_profile.cpp`).
- `GTest::gtest`, `GTest::gtest_main` (`tests/unit/CMakeLists.txt`).
- Protocol frame fixtures in `tests/unit/test_real_frames.cpp`.
- `voluptuous` - Schema validation in `custom_components/mitsubishi_climate_proxy/config_flow.py` and `climate.py`.
- Home Assistant core modules listed under Frameworks above.
- `custom_components/mitsubishi_climate_proxy/manifest.json` declares `"requirements": []` and `"dependencies": []`; it is a `config_flow: true` integration with `iot_class: local_polling`.
- ESPHome built-in components referenced by example configs: `wifi`, `api`, `ota`, `logger`, `time` (`platform: homeassistant` or `platform: sntp`), `web_server`, `captive_portal`, `mqtt`, `esp32_ble_tracker`, `bluetooth_proxy`, `remote_transmitter`, `dallas_temp`, `esp32_rmt_led_strip`, `st7789v` display, `spi`, `i2c`-based sensors.

## Configuration

- Per-device ESPHome YAML files. The component is enabled with:
- Remote source option: `github://echavet/MitsubishiCN105ESPHome` (commented alternative in `examples/root-configs/hp-debug.yaml` lines 88-90).
- Secrets: ESPHome `!secret` references load from `secrets.yaml`. `secrets.yaml` is git-ignored (`.gitignore` line 1). CI generates random placeholder values for the example configs (`.github/workflows/build.yaml`, "Generate random secret" step). Keys referenced include `wifi_ssid`, `wifi_password`, `ota_pwd`, `api_key`, `encryption_key`, `mqtt_password`. Values are not stored in the repository.
- Substitutions: `substitutions:` blocks are used in 11 example files for device naming.
- Config-flow based setup (`custom_components/mitsubishi_climate_proxy/config_flow.py`), with an Options flow for runtime toggles (coordinator single-target mode). No YAML configuration schema is required.
- Unit tests: `tests/unit/CMakeLists.txt` (standalone; compiles `components/cn105/heatpump_functions_protocol.cpp` plus test sources). Legacy-API variant built with `ESPHOME_VERSION_CODE=VERSION_CODE(2026,2,0)`.
- Firmware: no local build file; compile through `esphome compile <path>.yaml`.
- Git ignore: `tests/unit/build/` (`.gitignore` line 77) holds CMake build output.

## Platform Requirements

- Python 3 with ESPHome installed (or the ESPHome Docker image `ghcr.io/esphome/esphome`).
- CMake >= 3.14 and a C++17 compiler (GCC/Clang) for `tests/unit`.
- Network access to GitHub during CMake configure (GoogleTest `FetchContent`).
- Hardware for flashing: ESP32 / ESP32-S3 or ESP8266 board with a 3.3 V-compatible logic-level shifter for the CN105 5-pin connector (`README.md` line 9, `docs/cn105-esp32-level-shifter.jpg`).
- Firmware runs on the ESP microcontroller, connected to the heat pump indoor unit over the CN105 UART port, and to Wi-Fi.
- The Home Assistant integration (`custom_components/mitsubishi_climate_proxy`) runs inside a Home Assistant instance. Distributed through HACS (`hacs.json`) or manual copy.
- Alternative: since ESPHome 2026.4.0 the ESPHome core ships a native `mitsubishi_cn105` component (by @crnjan), described in `README.md` line 1300. This repository is a separate implementation of the same protocol.

<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->

## Conventions

## Naming Patterns

- C++ component sources and headers are snake_case or mixed-case, with no single rule: `components/cn105/request_scheduler.cpp`, `components/cn105/frame_parser.h`, `components/cn105/cn105_types.h`, but also `components/cn105/heatpumpFunctions.cpp`, `components/cn105/climateControls.cpp`, `components/cn105/Globals.h`. New files should use snake_case (the majority pattern in recent additions).
- Python integration files are lowercase: `components/cn105/climate.py`, `custom_components/mitsubishi_climate_proxy/config_flow.py`.
- Unit test files are `tests/unit/test_<topic>.cpp` (snake_case after `test_`), e.g. `tests/unit/test_setpoint_persistence.cpp`, `tests/unit/test_frame_parser.cpp`.
- Shared test stubs live under `tests/unit/mocks/` mirroring ESPHome include paths, e.g. `tests/unit/mocks/esphome/components/climate/climate_mode.h`.
- Per-sensor / per-entity headers in the component use the `<feature>_sensor.h` or `<feature>_select.h` pattern: `components/cn105/kwh_sensor.h`, `components/cn105/van_orientation_select.h`, `components/cn105/hvac_option_switch.h`.
- Main component: `CN105Climate` in `components/cn105/cn105.h`.
- ESPHome entity wrappers are PascalCase and inherit from both the ESPHome platform type and `Component`:
- Protocol and pure logic classes are PascalCase: `FrameParser` (`components/cn105/frame_parser.h`), `RequestScheduler` (`components/cn105/request_scheduler.h`), `HeatPumpProtocol`, `LossnayProtocol` (`components/cn105/lossnay_profile_protocol.h`).
- Plain data records use lowercase-initial or snake-ish struct names inherited from the original code: `heatpumpSettings`, `wantedHeatpumpSettings`, `heatpumpStatus`, `heatpumpRunStates` in `components/cn105/cn105_types.h`, plus `InfoRequest` in `components/cn105/info_request.h`. Do not rename these; new data structs should prefer PascalCase.
- Enums are `enum class` with UPPER_SNAKE_CASE values: `DriverState::CONNECTED`, `VaneType::SPLIT_HORIZONTAL` (`components/cn105/cn105.h`), `ProfileFeature::REMOTE_TEMPERATURE` (`components/cn105/protocol_profile.h`).
- Mixed style. The dominant style in `CN105Climate` is camelCase: `setTargetTemperatureLow()`, `getTargetTemperatureHigh()`, `checkSum()`, `setupUART()`, `isHeatpumpConnected()`, `hasChanged()`, `setPowerSetting()`.
- ESPHome-facing setters and getters use snake_case, matching ESPHome's own API: `set_vertical_vane_select()`, `set_compressor_frequency_sensor()`, `get_compressor_frequency()`, `set_vane_type()`, `set_hardware_settings_interval()` (`components/cn105/cn105.h`).
- Newer modules (`RequestScheduler`, `FrameParser`, `HeatPumpProtocol`) use snake_case: `register_request()`, `send_request()`, `clear_requests()`, `frame_complete()`, `saved_setpoint_state()`.
- Rule for new code: match the surrounding class. Use snake_case in new protocol/helper code and in anything exposed through the ESPHome `set_*`/`get_*` API; keep camelCase only inside existing `CN105Climate` methods.
- Private/protected members end with a trailing underscore: `current_request_index_`, `send_callback_`, `requests_`, `hp_uptime_connection_sensor_`, `remote_temp_margin_` (`components/cn105/request_scheduler.cpp`, `components/cn105/cn105.h`). `CN105Climate` has legacy members without the underscore (`autoUpdate`, `externalUpdate`, `wantedSettingsMutex`) — do not copy that style.
- Public member fields on `CN105Climate` also end in `_` (`iSee_sensor_`, `Functions_sensor_`). Mixed capitalization like `Functions_sensor_` is legacy.
- Null-initialize pointers at declaration: `sensor::Sensor* compressor_frequency_sensor_ = nullptr;`, or brace-init `text_sensor::TextSensor* stage_sensor_{ nullptr };`. Both forms appear; prefer `= nullptr`.
- Loop/index variables are `i`, `size_t i`, or `auto& req` for range loops over requests.
- Protocol constants are `static const` with UPPER_SNAKE_CASE in `components/cn105/cn105_types.h`: `CONNECT_LEN`, `CONNECT`, `HEADER`, `RQST_PKT_SETTINGS`, `RCVD_PKT_FAIL`, `DEFAULT_REMOTE_TEMP_KEEPALIVE_INTERVAL_MS`.
- Log tags are `static const char*` globals with `LOG_*_TAG` or `LOG_*` names, all defined in `components/cn105/cn105_types.h` (e.g. `LOG_CYCLE_TAG`, `LOG_CONN_TAG`, `LOG_HARDWARE_SELECT_TAG`). The main tag is `TAG = "CN105"`. Some files still use literal tags such as `ESP_LOGW("chkSum", ...)` and `ESP_LOGW("Decoder", ...)` in `components/cn105/hp_readings.cpp`; prefer a named constant in new code.
- Preprocessor `#define` is used only for two sizing values in `cn105_types.h` (`MAX_DATA_BYTES`, `MAX_DELAY_RESPONSE_FACTOR`). Prefer `static const` or `constexpr` for new constants.
- Python constants are `CONF_*` for YAML keys (`CONF_HORIZONTAL_VANES = "horizontal_vanes"`), `DEFAULT_*` for defaults, and `UPPER_SNAKE` for enumeration tuples (`TURN_ON_MODE_PRIORITY`, `VANE_TYPES`) in `components/cn105/climate.py`, `custom_components/mitsubishi_climate_proxy/climate.py`, and `custom_components/mitsubishi_climate_proxy/config_flow.py`.
- C++ production code lives in `namespace esphome { namespace cn105 { ... } }` (see `components/cn105/cn105.h`, `components/cn105/localization.h`).
- Pure protocol helpers live in `namespace cn105_protocol` (`components/cn105/frame_parser.h`, `components/cn105/cn105_protocol.h`), and unit tests use `using namespace cn105_protocol;`.
- Do not add new `using namespace` at file scope in headers. `components/cn105/request_scheduler.cpp` uses `using namespace esphome;` in a `.cpp`, which is acceptable only in implementation files.

## Code Style

- No formatter config is tracked (no `.clang-format`, `.editorconfig`, or `.prettierrc` at repo root). Match the surrounding file.
- Indentation is 4 spaces in C++ (`components/cn105/request_scheduler.cpp`, `components/cn105/frame_parser.h`) and in the `.h` headers, with 2-space indent in some legacy files. Python uses 4 spaces.
- Braces: K&R-style opening braces on the same line for functions and control flow are the norm (`void RequestScheduler::clear_requests() {`).
- Single-line guards are used: `if (req.code != code) continue;`, `if (req.disabled) { return; }`.
- Long function signatures are split one parameter per line (see the constructor in `components/cn105/request_scheduler.cpp`).
- No C++ linter or static-analysis config is tracked in the repository.
- Python has no `ruff`, `flake8`, `pylint`, or `black` configuration tracked; no `pyproject.toml` is present.
- The only automated check is the CI workflow in `.github/workflows/build.yaml`, which runs GoogleTest and compiles example ESPHome configs.
- Most C++ files open with a header comment in the form `/// <file>.h — <one-line role>` followed by `/// Deps:` and sometimes `/// Role:`. Examples: `tests/unit/test_checksum.cpp`, `tests/unit/test_dual_setpoint_recenter.cpp`, `components/cn105/frame_parser.h`.
- Python integration files use `## <file>.py — <description>` with `## Role:` and `## Deps:` lines (`components/cn105/climate.py`, `custom_components/mitsubishi_climate_proxy/climate.py`, `custom_components/mitsubishi_climate_proxy/config_flow.py`).
- Comments and documentation are written in both English and French. Existing French comments are not to be translated en masse. Write new comments in English.
- Section banners in tests use box-drawing rules: `// ════...` followed by a title (see `tests/unit/test_frame_parser.cpp`, `tests/unit/test_real_frames.cpp`).
- Issue numbers are referenced in comments when fixing regressions: `// Expose the existing polling-cycle state` in `components/cn105/cn105.h`, `(issue #733)` in `components/cn105/esphome_compat.h` and `custom_components/...`. Keep this for regression fixes.
- Doxygen-style `@class` / `@brief` appears in a few headers, e.g. `components/cn105/request_scheduler.h`. Optional.

## Import Organization

## Error Handling

- Errors are logged and execution returns early. No C++ `try`/`throw` usage appears in `components/cn105/`. Examples: `components/cn105/cn105.cpp` returns `false` from a check at around line 159, and `components/cn105/hp_readings.cpp` logs `ESP_LOGW(..., "KO -> checksum mismatch ...")` and drops the frame.
- ESP-IDF return codes are checked and logged with `esp_err_to_name()`: `ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(pin_err));` in `components/cn105/cn105.cpp`.
- Unknown or invalid inputs are logged as warnings and ignored, not asserted: `ESP_LOGW(LOG_HARDWARE_SELECT_TAG, "Code %d received unknown value: %d", ...)` in `components/cn105/hardware_setting_select.cpp`.
- Protocol parsing returns `bool` / `std::optional`-like results. `HeatPumpProtocol::restored_setpoint_state()` returns an optional-like value tested with `.has_value()` (`tests/unit/test_setpoint_persistence.cpp`).
- Overflow and bounds are guarded before indexing: `FrameParser::feed()` resets on `bytes_read_ >= MAX_DATA_BYTES` (`components/cn105/frame_parser.h`).
- Unused callback arguments are explicitly voided: `r_error_info.onResponse = [this](CN105Climate& self) { (void)self; ... };` in `components/cn105/cn105.cpp`.
- Use `ServiceValidationError` for user-facing service failures (imported in `custom_components/mitsubishi_climate_proxy/climate.py`).
- Parse user input defensively and fall back to defaults on `(TypeError, ValueError)`: see `custom_components/mitsubishi_climate_proxy/climate.py` near lines 225 and 346.
- Use `_LOGGER.warning` for recoverable configuration issues and `_LOGGER.debug` for state tracing (e.g. `custom_components/mitsubishi_climate_proxy/climate.py` lines 635, 673, 1015).

## Logging

- Pass the tag as a named constant from `components/cn105/cn105_types.h` (`LOG_CYCLE_TAG`, `LOG_SETTINGS_TAG`, `LOG_ACTION_EVT_TAG`).
- Use `ESP_LOGD` for per-frame or per-request traces (`components/cn105/request_scheduler.cpp`: `ESP_LOGD(tag, "Sending %s (0x%02X)", ...)`).
- Use `ESP_LOGI` for state transitions and user-visible decisions (`components/cn105/extraComponents.cpp`: `ESP_LOGI(TAG, "Fahrenheit compatibility mode: %s", ...)`).
- Use `ESP_LOGW` for unexpected protocol data, unknown values, or disabled-feature control attempts.
- Use `%02X` for command bytes and `%d` for codes; format with `%s` plus a ternary for booleans (`value ? "true" : "false"`).
- Do not log credentials or Wi-Fi/API secrets.

## Comments

- Explain protocol bytes and why a value is what it is, with a capture or source reference: `// Source: Live debug logs from atom-s3 ESP32 (2026-05-02 14:04-14:05)` (`tests/unit/test_real_frames.cpp`), `// Info request for settings (0x02): FC 42 01 30 10 02 ...` (`tests/unit/test_checksum.cpp`).
- Document regression history with the issue number and the bug mechanism, as in `tests/unit/test_setpoint_persistence.cpp` and `tests/unit/test_dual_setpoint_recenter.cpp` (issue #673).
- Mark deliberate compatibility shims (`components/cn105/esphome_compat.h`, `components/cn105/climate.py` `_esphome_version_tuple`).

## Function Design

- Pass protocol buffers as `uint8_t data[]` plus `int len` (`checkSum(uint8_t bytes[], int len)` in `components/cn105/heatpumpFunctions.cpp`).
- Use `const char*` for log/labels and `esphome::StringRef` overloads where ESPHome passes that type (`hasChanged` overloads in `components/cn105/cn105.h`).
- Callbacks are `std::function`-based typedefs (`SendCallback`, `TimeoutCallback`, `TerminateCallback`, `ContextCallback` in `components/cn105/request_scheduler.h`).
- Boolean predicates named `is*`, `has*`, `should*`: `isHeatpumpConnected()`, `hasChanged()`, `is_empty()`.
- Use `float NAN` for "no value" temperature fields (`components/cn105/cn105_types.h` struct defaults, `tests/unit/test_dual_setpoint_recenter.cpp` uses `NAN` for unset low/high).
- Use `static_cast<uint8_t>(...)` when comparing enum values to stored `uint8_t` fields (`tests/unit/test_setpoint_persistence.cpp`).

## Module Design

- Public API is declared in the class header; implementation is in the matching `.cpp`. Header-only ESPHome entities are the exception: `components/cn105/*_sensor.h`, `*_select.h`, `*_switch.h`, `*_button.h`, `*_number.h` are header-only classes.
- Standalone pure modules (no ESPHome dependency) are kept testable: `components/cn105/frame_parser.h`, `components/cn105/cn105_protocol.h`, `components/cn105/heatpump_profile_protocol.h`, `components/cn105/lossnay_profile_protocol.h`. New protocol logic goes in these, not in `CN105Climate`.
- `CN105Climate` declares `friend class cn105::HeatPumpProfile;` and `friend class cn105::LossnayProfile;` (`components/cn105/cn105.h`). Do not add new friends; expose a method instead.

<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->

## Architecture

## System Overview

```text

```
- `custom_components/mitsubishi_climate_proxy/` is a separate Home Assistant custom integration (`manifest.json` domain `mitsubishi_climate_proxy`, version `2026.5.2`) that wraps an ESPHome CN105 climate entity in a Python `ClimateEntity` (`climate.py`, 1080 lines) with a config flow (`config_flow.py`). It is not loaded by the firmware.
- `tests/unit/` builds a standalone GoogleTest binary that compiles pure protocol sources from `components/cn105/` against stub headers in `tests/unit/mocks/`. No ESPHome runtime is linked.

## Pattern Overview

- Single `CN105Climate` class (`components/cn105/cn105.h`) is the ESPHome `climate::Climate`, `Component`, and `uart::UARTDevice`. It owns transport, scheduling, state publishing, and the connection FSM. It is the composition root for every sub-object.
- Device dialect is chosen at construction: `make_protocol_profile(*this, lossnay)` in `components/cn105/protocol_profile.cpp` returns either `HeatPumpProfile` (`0x30`) or `LossnayProfile` (`0x34`). The profile implements every device-specific decode/encode/control hook declared in `components/cn105/protocol_profile.h`.
- Polling is cooperative: `loop()` drains UART bytes one at a time, feeds them to `FrameParser`, and at most starts one request cycle per `update_interval`. There are no blocking reads or threads on the default path.
- Writes are queued as "wanted" state (`wantedHeatpumpSettings`, `wantedHeatpumpRunStates`) and flushed on a later cycle, so UI commands and remote temperature updates are coalesced rather than sent immediately.
- Mutual exclusion is a `std::mutex` on ESP32 (`wantedSettingsMutex`, `esp32Mutex`) and a `volatile bool` emulation on ESP8266 (`emulateMutex` in `components/cn105/cn105.h`).
- Configuration is declarative YAML validated in Python (`components/cn105/climate.py` `CONFIG_SCHEMA`), then emitted as setter calls (`cg.add(var.set_...)`) in `to_code`.

## Layers

- Purpose: Validate YAML, create the C++ object, wire sensors/selects/switches/buttons/numbers and UART pins.
- Location: `components/cn105/climate.py`, `components/cn105/__init__.py` (empty; the package marker for ESPHome external components).
- Contains: schema constants (`CONF_*`), `CONFIG_SCHEMA`, `validate_lossnay_config`, `to_code`, `get_uart_pins_from_config`, `get_uart_port_index`, `_esphome_version_tuple` (ESPHome API-version shim, see issue #733 referenced in code).
- Depends on: ESPHome Python APIs (`esphome.codegen`, `esphome.config_validation`, component modules `climate`, `uart`, `sensor`, `select`, `button`, `switch`, `number`, `binary_sensor`, `text_sensor`, `uptime`).
- Used by: ESPHome compiler when a YAML config contains `climate: - platform: cn105`.
- Purpose: Expose ESPHome climate entity behaviour (`traits()`, `control()`) and setter hooks used by codegen.
- Location: `components/cn105/cn105.h` (class declaration), `components/cn105/climateControls.cpp` (`control`, `controlDelegate`, `traits`, `processModeChange`, `processTemperatureChange`, `processFanChange`, `processSwingChange`), `components/cn105/extraComponents.cpp` (all `set_*_sensor`/`set_*_select`/`set_*_switch` wiring).
- Contains: ESPHome `ClimateCall` translation, traits advertisement, optional entity pointers (null when not configured).
- Depends on: `esphome/components/climate/*`, the optional entity classes in `components/cn105/*.h`.
- Used by: ESPHome Home Assistant API, MQTT, web server, YAML lambdas.
- Purpose: Drive the CN105 handshake and recover from disconnects.
- Location: `components/cn105/cn105.cpp` (`transition_to_`, `setupUART`, `disconnectUART`, `reconnectUART`, `reconnectIfConnectionLost`, `isHeatpumpConnectionActive`, `force_low_level_uart_reinit`), `components/cn105/componentEntries.cpp` (`setup`, `loop`, `maybe_start_connection_`, `set_update_interval`), `components/cn105/hp_readings.cpp` (`handleConnectionSuccess`).
- Contains: `DriverState` enum (`BOOT`, `WAIT_WIFI`, `WAIT_GRACE`, `CONNECTING`, `CONNECTED`, `DISCONNECTED`) declared in `components/cn105/cn105.h`. Transitions are logged under the `FSM` tag.
- Depends on: ESP-IDF UART driver (`driver/uart.h`, guarded by `USE_ESP32`) for low-level reinit.
- Used by: `loop()` gates all write cycles on `isHeatpumpConnected()`.
- Purpose: Decide which INFO codes to request in each cycle and time out stuck requests.
- Location: `components/cn105/request_scheduler.h/.cpp` (`RequestScheduler`), `components/cn105/info_request.h` (`InfoRequest` struct), `components/cn105/cycle_management.h/.cpp` (`cycleManagement` struct), `components/cn105/cn105.cpp` (`registerInfoRequests`, `registerHardwareSettingsRequests`), `components/cn105/hp_writings.cpp` (`buildAndSendRequestsInfoPackets`, `buildAndSendInfoPacket`).
- Contains: registered requests keyed by code (`0x02` settings, `0x03` room temp, `0x06` status, plus functions/timers/standby/hvac-option codes), per-request `canSend` and `onResponse` callbacks, soft and hard timeouts via ESPHome `set_timeout`.
- Depends on: callbacks injected from `CN105Climate` in its constructor (`cn105.cpp` lines 31–45). The scheduler never includes `CN105Climate` directly beyond a forward declaration.
- Used by: `loop()` via `buildAndSendRequestsInfoPackets()` when `loopCycle.hasUpdateIntervalPassed(...)`.
- Purpose: Parse byte streams and encode or decode CN105 frames without ESPHome dependencies.
- Location: `components/cn105/frame_parser.h` (`cn105_protocol::FrameParser`), `components/cn105/cn105_protocol.h` (checksum `(0xFC - sum) & 0xFF`, temperature encode/decode, byte-map lookups), `components/cn105/cn105_types.h` (`heatpumpSettings`, `wantedHeatpumpSettings`, `heatpumpStatus`, `heatpumpRunStates`), `components/cn105/heatpump_profile_protocol.h` (`HeatPumpProtocol`, `SetpointState` persistence and deadband helpers), `components/cn105/lossnay_profile_protocol.h` (`LossnayProtocol`, `LOSSNAY_PROFILE = 0x34`), `components/cn105/heatpumpFunctions.h` plus `heatpump_functions_protocol.cpp`, `components/cn105/localization.h`.
- Depends on: `<cstdint>`, `<cmath>`, `<cstring>`, `<optional>`; `heatpump_profile_protocol.h` also uses `esphome/components/climate/climate_mode.h`.
- Used by: `CN105Climate::processInput`, the profiles, and the unit tests (`tests/unit/test_*.cpp`).
- Purpose: Implement dialect-specific decoding, encoding, and control policy.
- Location: Base interface `components/cn105/protocol_profile.h` (`Cn105ProtocolProfile`, `ProfileFeature` enum). Implementations `components/cn105/heatpump_profile.h/.cpp` (`HeatPumpProfile`, 1274-line implementation, dual-setpoint, vane, and remote temperature logic) and `components/cn105/lossnay_profile.h/.cpp` (`LossnayProfile`, 114 lines). Shared default `Cn105ProtocolProfile::decode_room_status` lives in `components/cn105/protocol_profile.cpp`.
- Depends on: `CN105Climate&` (held as `hp_`, and `friend class` access declared in `cn105.h`), so profiles mutate the climate's `currentSettings`, `wantedSettings`, and entity pointers directly.
- Used by: `CN105Climate::setup`, `processCommand`, `control`, `loop` through `profile_->...` calls.
- Purpose: Turn decoded frames into ESPHome state and turn wanted state into outbound frames.
- Location: Reads `components/cn105/hp_readings.cpp` (`processInput`, `processDataPacket`, `getDataFromResponsePacket`, `getSettingsFromResponsePacket`, `getRoomTemperatureFromResponsePacket`, `statusChanged`, `publishStateToHA`, `heatpumpUpdate`, `terminateCycle`). Writes `components/cn105/hp_writings.cpp` (`prepareInfoPacket`, `prepareSetPacket`, `writePacket`, `try_write_pending_packet`, `sendWantedSettings`, `sendRemoteTemperature`).
- Depends on: `FrameParser`, the profile, the UART `write_byte` API.
- Used by: `loop()` and `control()`.
- Purpose: Expose diagnostics and vane/option controls as separate ESPHome entities that the climate writes to.
- Location: One header per entity in `components/cn105/`: `compressor_frequency_sensor.h`, `input_power_sensor.h`, `kwh_sensor.h`, `runtime_hours_sensor.h`, `outside_air_temperature_sensor.h`, `target_humidity_sensor.h`, `isee_sensor.h`, `stage_sensor.h`, `sub_mode_sensor.h`, `auto_sub_mode_sensor.h`, `error_code_sensor.h`, `remote_temp_source_info.h`, `uptime_connection_sensor.h/.cpp`, `van_orientation_select.h`, `hardware_setting_select.h/.cpp`, `hvac_option_switch.h`, `functions_button.h`, `functions_number.h`, `functions_sensor.h`, `climate_action.h`.
- Depends on: ESPHome `sensor`, `select`, `switch`, `button`, `number`, `binary_sensor`, `text_sensor` component bases.
- Used by: pointers stored on `CN105Climate` and updated from profile decoding (`updateExtraSelectComponents` in `heatpump_profile.cpp`).
- Purpose: Present the CN105 climate with Home Assistant-specific behaviour (dual setpoint presentation, F/C mapping, horizontal and vertical vane entities, optional single-target coordinator mode, restore last mode).
- Location: `custom_components/mitsubishi_climate_proxy/climate.py`, `custom_components/mitsubishi_climate_proxy/config_flow.py`, `custom_components/mitsubishi_climate_proxy/__init__.py` (`async_setup_entry` forwards to the `climate` platform).
- Depends on: `homeassistant.components.climate`, `homeassistant.helpers.event`, `homeassistant.helpers.restore_state`. Wraps an existing ESPHome climate entity id via `CONF_SOURCE_ENTITY`.
- Used by: Home Assistant users who install the integration; not required for firmware operation.

## Data Flow

### Primary Request Path (polling read)

### Command Path (user changes a setting)

### Remote Temperature Path

### Connection Lifecycle

- Device state lives in value structs on the climate object: `currentSettings`, `wantedSettings`, `currentStatus`, `currentRunStates`, `wantedRunStates`, `functions` (`components/cn105/cn105.h`).
- Cycle state is `loopCycle` (`cycleManagement`) and scheduler state is `scheduler_` (`RequestScheduler`).
- Persistent state uses ESPHome preferences: `setpoint_pref_` (`ESPPreferenceObject`) in `HeatPumpProfile` (`components/cn105/heatpump_profile.h`).

## Key Abstractions

- Purpose: The single ESPHome climate entity and orchestrator for one CN105 port.
- Examples: `components/cn105/cn105.h`, `components/cn105/cn105.cpp`, `components/cn105/componentEntries.cpp`, `components/cn105/hp_readings.cpp`, `components/cn105/hp_writings.cpp`, `components/cn105/climateControls.cpp`, `components/cn105/extraComponents.cpp`, `components/cn105/heatpumpFunctions.cpp`
- Pattern: Partial-class-by-concern. One class is split across multiple `.cpp` files grouped by responsibility. There is no separate facade.
- Purpose: Strategy interface for device dialects. Hooks cover settings decode, status decode, control encode, fan and temperature policy, swing policy, and feature gating via `supports()` and `allow_operation()`.
- Examples: `components/cn105/protocol_profile.h`, `components/cn105/heatpump_profile.h`, `components/cn105/lossnay_profile.h`
- Pattern: Virtual strategy with a factory (`make_protocol_profile`). Feature gating uses `ProfileFeature` enum values (`TEMPERATURE`, `SWING`, `REMOTE_TEMPERATURE`, `AUXILIARY_CONTROLS`, `INSTALLER_MODE`).
- Purpose: Round-robin queue of INFO requests with per-request interval, failure counting, and optional `canSend`/`onResponse` hooks.
- Examples: `components/cn105/request_scheduler.h`, `components/cn105/request_scheduler.cpp`, `components/cn105/info_request.h`
- Pattern: Command queue with callbacks injected via `std::function` from the owning climate (`cn105.cpp` constructor).
- Purpose: Byte-by-byte assembly of CN105 frames (`0xFC` start, command, length, payload, checksum), with overflow and sanity checks.
- Examples: `components/cn105/frame_parser.h`
- Pattern: Stateful push parser with no ESPHome dependency, tested in `tests/unit/test_frame_parser.cpp`.
- Purpose: Tracks whether a polling cycle is running, when the last cycle started/completed, and timeout detection.
- Examples: `components/cn105/cycle_management.h`, `components/cn105/cycle_management.cpp`
- Pattern: Plain struct with imperative methods; `deferCycle()` reschedules writes when a cycle is in flight.
- Purpose: Testable encode/decode and lookup functions in the `cn105_protocol` namespace.
- Examples: `components/cn105/cn105_protocol.h`, `components/cn105/heatpump_profile_protocol.h`, `components/cn105/lossnay_profile_protocol.h`
- Pattern: Header-only `inline` functions and `static` tables, with no ESPHome includes on the protocol-only path.
- Purpose: Diagnostic sensors, vane selects, option switches, and function buttons/numbers as independent ESPHome components referenced by pointer.
- Examples: `components/cn105/compressor_frequency_sensor.h`, `components/cn105/van_orientation_select.h`, `components/cn105/hvac_option_switch.h`, `components/cn105/functions_button.h`
- Pattern: Each extends an ESPHome base (`sensor::Sensor`, `select::Select`, etc.) and is null-checked before use in the climate.

## Entry Points

- Location: `components/cn105/climate.py` (`CONFIG_SCHEMA`, `to_code`), plus `components/cn105/__init__.py` as the package marker.
- Triggers: ESPHome compile of any YAML containing `climate: - platform: cn105`. Example configurations in `examples/root-configs/*.yaml`, `examples/xiao-esp32s3/heatpump-xiaoESP32S3.yaml`, `examples/m5stack-atoms3/atom-m5.yaml`, `examples/modular-hvac/hvac-sample.yaml`.
- Responsibilities: Validate config, instantiate `CN105Climate` with the UART component, emit setter calls.
- Location: `components/cn105/componentEntries.cpp` (`setup`, `loop`), `components/cn105/climateControls.cpp` (`control`, `traits`).
- Triggers: ESPHome main loop and climate command from the API or MQTT.
- Responsibilities: Non-blocking UART drain, cycle start, command queueing.
- Location: `components/cn105/hp_readings.cpp` (`processInput`, `processDataPacket`).
- Triggers: Every byte received on the configured UART (`tx_pin`/`rx_pin` set via `set_tx_rx_pins`).
- Responsibilities: Frame assembly, checksum, dispatch.
- Location: `custom_components/mitsubishi_climate_proxy/__init__.py` (`async_setup_entry`), `custom_components/mitsubishi_climate_proxy/config_flow.py`.
- Triggers: Home Assistant config entry load.
- Responsibilities: Forward to the `climate` platform, reload on options change.
- Location: `tests/unit/CMakeLists.txt` (`cn105_tests` executable, `gtest_discover_tests`).
- Triggers: `cmake -B tests/unit/build -S tests/unit` then `ctest` (also run in `.github/workflows/build.yaml` job `unit-tests`, which gates the ESPHome compile matrix).

## Architectural Constraints

- **Threading:** Single-threaded ESPHome main loop for all protocol work. UART bytes are drained synchronously inside `loop()`. ESP32 builds use `std::mutex` for `wantedSettingsMutex` and `esp32Mutex` (`components/cn105/cn105.h`). ESP8266 builds emulate this with `volatile bool` flags (`emulateMutex`).
- **Global state:** `components/cn105/Globals.h` defines `static const char*` log tags and `#define MAX_DATA_BYTES 64`, `#define MAX_DELAY_RESPONSE_FACTOR 10`. These are per-translation-unit statics, duplicated in every TU that includes the header. `components/cn105/heatpumpFunctions.cpp` and `components/cn105/cn105.cpp` rely on this shared constant set.
- **Circular imports:** `components/cn105/cn105.h` includes every entity header, and entity headers forward-declare or include `cn105.h` back (for example `components/cn105/request_scheduler.h` forward-declares `CN105Climate`, `components/cn105/info_request.h` uses a forward declaration). Profiles and the climate class are mutually dependent via `friend class cn105::HeatPumpProfile` and `friend class cn105::LossnayProfile` declared in `components/cn105/cn105.h`.
- **Protocol purity boundary:** Code under `components/cn105/` that is pure (`cn105_protocol.h`, `frame_parser.h`, `heatpump_profile_protocol.h`, `lossnay_profile_protocol.h`, `heatpumpFunctions.h`) must not include ESPHome headers beyond `esphome/components/climate/climate_mode.h`. The unit-test build depends on this.
- **Build flags:** Code is gated by `USE_ESP32`, `ESPHOME_VERSION_CODE`, and `TEST_MODE`. The compatibility shim `components/cn105/esphome_compat.h` selects ESPHome 2025.11+ `ClimateTraits` feature flags versus legacy `supports_*` accessors.
- **Lossnay mode switch:** `lossnay: true` in YAML selects `LossnayProfile` at construction (`make_protocol_profile`). Changing it requires recompile; there is no runtime switch.
- **Installer mode:** `installer_mode` in YAML sends CONNECT `0x5B` instead of `0x5A` and may fall back to standard mode if the heat pump ignores it (`set_installer_mode` in `components/cn105/cn105.h`).

## Anti-Patterns

### God class across many translation units

### Duplicate or stale protocol-state flags

### Writing raw protocol bytes outside the write path

### Mojibake comments in source

## Error Handling

- Checksum mismatch and malformed frames are dropped by `FrameParser` (`components/cn105/frame_parser.h`) and `processDataPacket` (`components/cn105/hp_readings.cpp`).
- Writes while disconnected are queued in `pending_packet_` and retried after 4 s (`components/cn105/hp_writings.cpp`). Packets larger than `PACKET_LEN` (22) are dropped with an error.
- Request timeouts increment `InfoRequest::failures`; once `maxFailures` is reached the request is disabled (`components/cn105/info_request.h`).
- Cycle timeouts force `cycleEnded(true)` (`components/cn105/cycle_management.cpp`, `checkTimeout`).

## Cross-Cutting Concerns

<!-- GSD:architecture-end -->

<!-- GSD:skills-start source:skills/ -->

## Project Skills

No project skills found. Add skills to any of: `.claude/skills/`, `.agents/skills/`, `.cursor/skills/`, `.github/skills/`, or `.codex/skills/` with a `SKILL.md` index file.
<!-- GSD:skills-end -->

<!-- GSD:workflow-start source:GSD defaults -->

## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd-fast` for a trivial task inline, with no subagents and no PLAN.md
- `/gsd-quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd-debug` for investigation and bug fixing
- `/gsd-execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->

<!-- GSD:profile-start -->

## Developer Profile

> Profile not yet configured. Run `/gsd-profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
