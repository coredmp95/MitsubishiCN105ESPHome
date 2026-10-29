---
last_mapped_commit: 5250ba782c261ae98fe54abd2475ae7ea7836475
last_mapped_at: 2026-10-27
---
# Technology Stack

**Analysis Date:** 2026-10-27

## Languages

**Primary:**
- C++ (C++17) - Core ESPHome external component implementing the CN105 heat pump protocol. Sources in `components/cn105/*.cpp` and headers in `components/cn105/*.h`. C++17 is set in `tests/unit/CMakeLists.txt` (`CMAKE_CXX_STANDARD 17`).

**Secondary:**
- Python 3 - ESPHome component code-generation and YAML validation (`components/cn105/__init__.py`, `components/cn105/climate.py`), and the Home Assistant custom integration (`custom_components/mitsubishi_climate_proxy/*.py`).
- YAML - ESPHome device configurations in `examples/**/*.yaml` (e.g. `examples/root-configs/hp-sejour.yaml`, `examples/m5stack-atoms3/atom-m5.yaml`, `examples/modular-hvac/hvac-sample.yaml`).
- JSON - Home Assistant integration metadata and translations: `custom_components/mitsubishi_climate_proxy/manifest.json`, `custom_components/mitsubishi_climate_proxy/strings.json`, `custom_components/mitsubishi_climate_proxy/icons.json`, `custom_components/mitsubishi_climate_proxy/translations/en.json`, `hacs.json`.
- CMake - Standalone C++ unit-test build in `tests/unit/CMakeLists.txt`.

## Runtime

**Target Firmware Environment:**
- ESPHome framework (external component, loaded via `external_components:`). Minimum supported ESPHome is 2025.7 per `README.md` (line 45); 2025.11+ recommended. Compile-time version shims exist for 2025.7 through 2026.x (`components/cn105/esphome_compat.h`, `components/cn105/hardware_setting_select.cpp`, `components/cn105/climate_action.h`, `components/cn105/uptime_connection_sensor.cpp`).
- Microcontroller targets:
  - ESP32 family (ESP32, ESP32-S3): `components/cn105/cn105.cpp` uses FreeRTOS (`#include <freertos/FreeRTOS.h>`, `<freertos/task.h>`) under `#ifdef USE_ESP32`.
  - ESP8266 (e.g. D1 Mini, ESP-01S 1MB): supported via `#ifndef USE_ESP32` branches in `components/cn105/cn105.h` and `components/cn105/utils.cpp`. Example configs use `board: d1_mini` and `board: esp01_1m` (`examples/root-configs/hp-sejour.yaml`, `examples/root-configs/hp-debug.yaml`).
- Firmware frameworks: ESP-IDF (`framework: type: esp-idf`) is used in most examples; Arduino is also supported (`examples/root-configs/esp32-test.yaml` shows a commented `type: arduino` alternative; `README.md` line 5 states compatibility with Arduino and ESP-IDF).

**Package Manager (Python tooling):**
- pip (implicit, via ESPHome). No `requirements.txt` or `pyproject.toml` exists in the repository.
- Lockfile: Not applicable (no Python or C++ lockfile).

**Build Image (CI):**
- `ghcr.io/esphome/esphome:stable` container (`.github/workflows/build.yaml`).

## Frameworks

**Core:**
- ESPHome (external component framework) - Provides `esphome::Component`, `climate::Climate`, `uart::UARTDevice`, `sensor`, `text_sensor`, `binary_sensor`, `switch`, `select`, `number`, `button`, `uptime`, and `preferences`. Imported in Python as `esphome.codegen`, `esphome.config_validation`, and `esphome.components.*` (`components/cn105/climate.py`).
- Home Assistant custom integration API - `homeassistant.config_entries`, `homeassistant.components.climate`, `homeassistant.components.select`, `homeassistant.helpers.event`, `homeassistant.helpers.restore_state` (`custom_components/mitsubishi_climate_proxy/`). No version pinned; `README.md` for the proxy notes HA 2024.12+ is required for `swing_horizontal_mode`.

**Testing:**
- GoogleTest v1.15.2 - Fetched automatically with CMake `FetchContent` from `https://github.com/google/googletest.git` (`tests/unit/CMakeLists.txt`).
- CTest - Test discovery via `gtest_discover_tests` (`tests/unit/CMakeLists.txt`).
- ESPHome stubs - Hand-written mocks under `tests/unit/mocks/` (`tests/unit/mocks/esphome_stubs.h`, `tests/unit/mocks/esphome/core/version.h` defaults `ESPHOME_VERSION_CODE` to 2026.3.0, `tests/unit/mocks/esphome/components/climate/climate_mode.h`).

**Build/Dev:**
- CMake >= 3.14 - Unit test build system (`tests/unit/CMakeLists.txt`).
- ESPHome CLI (`esphome compile <config>.yaml`) - Firmware compilation, used by CI (`.github/workflows/build.yaml`).
- GitHub Actions - CI (`.github/workflows/build.yaml`), using `actions/checkout@v5`.
- VS Code - Editor settings in `.vscode/settings.json` (C++ file associations for standard library headers).
- HACS - Distribution metadata for the Home Assistant integration (`hacs.json`, `render_readme: true`, `zip_release: false`, `content_in_root: false`).

## Key Dependencies

**Critical (ESPHome-provided, linked at firmware build time):**
- `esphome/core/*` (`component.h`, `log.h`, `helpers.h`, `preferences.h`, `version.h`, `macros.h`) - Core runtime, logging, persistent storage. Used throughout `components/cn105/`.
- `esphome/components/uart/uart.h` - UART transport to the heat pump CN105 connector. Configured by `components/cn105/climate.py` with 8 data bits, even parity, 1 stop bit (lines 606-608); baud rate is set on the UART component, not on the climate component (`components/cn105/climate.py` line 498 rejects `baud_rate`).
- `esphome/components/climate/*` - `ClimateTraits`, `climate_mode.h`, `climate_traits.h`.
- `esphome/components/{sensor,text_sensor,select,uptime}/*` - Entity types exposed by the component.

**Vendored / Adapted Protocol Code:**
- `components/cn105/heatpumpFunctions.cpp`, `components/cn105/heatpumpFunctions.h`, `components/cn105/Globals.h` - Adapted from the SwiCago HeatPump Arduino library (referenced in `README.md` line 39). Also credits geoffdavis/esphome-mitsubishiheatpump (`README.md` line 7).
- `components/cn105/*_profile*.{cpp,h}` - Heat pump / Lossnay model profiles (`heatpump_profile.cpp`, `lossnay_profile.cpp`, `protocol_profile.cpp`).

**Test-only:**
- `GTest::gtest`, `GTest::gtest_main` (`tests/unit/CMakeLists.txt`).
- Protocol frame fixtures in `tests/unit/test_real_frames.cpp`.

**Home Assistant integration (Python, provided by Home Assistant core at runtime):**
- `voluptuous` - Schema validation in `custom_components/mitsubishi_climate_proxy/config_flow.py` and `climate.py`.
- Home Assistant core modules listed under Frameworks above.
- `custom_components/mitsubishi_climate_proxy/manifest.json` declares `"requirements": []` and `"dependencies": []`; it is a `config_flow: true` integration with `iot_class: local_polling`.

**Infrastructure / Firmware Libraries used by examples (declared in YAML, not in this repo):**
- ESPHome built-in components referenced by example configs: `wifi`, `api`, `ota`, `logger`, `time` (`platform: homeassistant` or `platform: sntp`), `web_server`, `captive_portal`, `mqtt`, `esp32_ble_tracker`, `bluetooth_proxy`, `remote_transmitter`, `dallas_temp`, `esp32_rmt_led_strip`, `st7789v` display, `spi`, `i2c`-based sensors.

## Configuration

**Firmware configuration:**
- Per-device ESPHome YAML files. The component is enabled with:
  ```yaml
  external_components:
    - source:
        type: local
        path: ../../components
  uart:
    id: HP_UART
    ...
  climate:
    - platform: cn105
  ```
  (Pattern from `examples/root-configs/hp-debug.yaml`, `examples/modular-hvac/packages/hvac-base.yaml`, `examples/dual-setpoint/common/climate-common-non-climate.yaml`.)
- Remote source option: `github://echavet/MitsubishiCN105ESPHome` (commented alternative in `examples/root-configs/hp-debug.yaml` lines 88-90).
- Secrets: ESPHome `!secret` references load from `secrets.yaml`. `secrets.yaml` is git-ignored (`.gitignore` line 1). CI generates random placeholder values for the example configs (`.github/workflows/build.yaml`, "Generate random secret" step). Keys referenced include `wifi_ssid`, `wifi_password`, `ota_pwd`, `api_key`, `encryption_key`, `mqtt_password`. Values are not stored in the repository.
- Substitutions: `substitutions:` blocks are used in 11 example files for device naming.

**Home Assistant integration configuration:**
- Config-flow based setup (`custom_components/mitsubishi_climate_proxy/config_flow.py`), with an Options flow for runtime toggles (coordinator single-target mode). No YAML configuration schema is required.

**Build / Test:**
- Unit tests: `tests/unit/CMakeLists.txt` (standalone; compiles `components/cn105/heatpump_functions_protocol.cpp` plus test sources). Legacy-API variant built with `ESPHOME_VERSION_CODE=VERSION_CODE(2026,2,0)`.
- Firmware: no local build file; compile through `esphome compile <path>.yaml`.
- Git ignore: `tests/unit/build/` (`.gitignore` line 77) holds CMake build output.

## Platform Requirements

**Development:**
- Python 3 with ESPHome installed (or the ESPHome Docker image `ghcr.io/esphome/esphome`).
- CMake >= 3.14 and a C++17 compiler (GCC/Clang) for `tests/unit`.
- Network access to GitHub during CMake configure (GoogleTest `FetchContent`).
- Hardware for flashing: ESP32 / ESP32-S3 or ESP8266 board with a 3.3 V-compatible logic-level shifter for the CN105 5-pin connector (`README.md` line 9, `docs/cn105-esp32-level-shifter.jpg`).

**Production / Deployment:**
- Firmware runs on the ESP microcontroller, connected to the heat pump indoor unit over the CN105 UART port, and to Wi-Fi.
- The Home Assistant integration (`custom_components/mitsubishi_climate_proxy`) runs inside a Home Assistant instance. Distributed through HACS (`hacs.json`) or manual copy.
- Alternative: since ESPHome 2026.4.0 the ESPHome core ships a native `mitsubishi_cn105` component (by @crnjan), described in `README.md` line 1300. This repository is a separate implementation of the same protocol.

---

*Stack analysis: 2026-10-27*
