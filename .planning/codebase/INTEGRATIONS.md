---
last_mapped_commit: 5250ba782c261ae98fe54abd2475ae7ea7836475
last_mapped_at: 2026-10-27
---
# External Integrations

**Analysis Date:** 2026-10-27

## APIs & External Services

**Heat Pump Hardware (primary external interface):**
- Mitsubishi Electric CN105 serial protocol - Bidirectional UART link to the indoor unit / Lossnay ventilator.
  - Transport: UART, 8 data bits, even parity, 1 stop bit, baud set on the UART component (`components/cn105/climate.py` lines 606-608; `components/cn105/cn105.cpp` lines 326-336 log and validate the settings).
  - Implementation: `components/cn105/cn105.cpp`, `components/cn105/cn105_protocol.h`, `components/cn105/cn105_types.h`, `components/cn105/frame_parser.h`, `components/cn105/heatpumpFunctions.cpp`, `components/cn105/info_request.cpp`, `components/cn105/request_scheduler.cpp`.
  - Auth: None (physical connector).
  - Protocol origin: SwiCago HeatPump library (`README.md` line 39).

**GitHub (build-time and source distribution):**
- `github://echavet/MitsubishiCN105ESPHome` - Optional ESPHome `external_components` source (`examples/root-configs/hp-debug.yaml` lines 88-90, `examples/m5stack-atoms3/includes/climate-config.yaml` lines 4-6, `examples/dual-setpoint/common/climate-common-non-climate.yaml` lines 80-81).
- GitHub Container Registry - `ghcr.io/esphome/esphome:stable` used as CI build container (`.github/workflows/build.yaml`).
- GitHub Actions - CI workflow `.github/workflows/build.yaml` (triggers: push to `main`, `workflow_dispatch`, `pull_request`).
- GoogleTest source - Fetched at CMake configure time from `https://github.com/google/googletest.git` at tag `v1.15.2` (`tests/unit/CMakeLists.txt`). CI-only dependency for the unit test job.

**Home Assistant (primary consumer):**
- Home Assistant native API via ESPHome `api:` component - Exposes climate, sensors, text sensors, switches, selects, numbers, buttons, and binary sensors to Home Assistant. Examples: `api:` in all `examples/**` device configs, `examples/modular-hvac/packages/hvac-base.yaml` line 12.
- Home Assistant time source - `time: - platform: homeassistant` in example configs (e.g. `examples/root-configs/hp-debug.yaml`, `examples/dual-setpoint/common/time.yaml`).
- Home Assistant entity-state input (remote temperature) - Native sensor binding: a Home Assistant sensor is referenced from the climate config and its value is forwarded to the heat pump as the external temperature (`README.md` line 825, PR #624). Configured through `remote_temp_source_info.h` and `components/cn105/climate.py`.
- Home Assistant custom integration (`custom_components/mitsubishi_climate_proxy`) - Reads source ESPHome climate entities via `async_track_state_change_event` (`custom_components/mitsubishi_climate_proxy/climate.py` line 31) and exposes a proxy `climate` entity. Restores state with `RestoreEntity` (line 32). Calls HA `ServiceValidationError` (line 33). Forwards to the `climate` platform via `hass.config_entries.async_forward_entry_setups` (`custom_components/mitsubishi_climate_proxy/__init__.py`).
- Apple HomeKit / Google Home (through Home Assistant): The proxy comments state that it preserves the HomeKit accessory ID across option changes (`custom_components/mitsubishi_climate_proxy/__init__.py` lines 21-24, `config_flow.py` line 109). No direct HomeKit or Google code is present in the repository.

**Other ESPHome-configured external services (example configs only):**
- MQTT broker - `mqtt:` block with `discovery: true` (`examples/m5stack-atoms3/includes/mqtt-config.yaml`, `examples/root-configs/hp-debug.yaml`, `examples/root-configs/esp32-test.yaml`, `examples/dual-setpoint/common/climate-common-non-climate.yaml`). Broker host and credentials come from `!secret` (`mqtt_password`, `broker`). Not required by the component itself.
- NTP / SNTP - Not referenced directly; time is sourced from Home Assistant in examples.
- Dallas temperature sensors (1-Wire) and `remote_transmitter` / `remote_receiver` components - Used in some examples (`examples/**/*.yaml`); hardware-level, not network services.
- Bluetooth proxy (`bluetooth_proxy:`, `esp32_ble_tracker:`) - Enabled in `examples/xiao-esp32s3/common/bt-proxy.yaml` and `examples/m5stack-atoms3/includes/bt-proxy-config.yaml`.

## Data Storage

**Databases:**
- None. The component does not use a database.

**Persistent device storage:**
- ESPHome `preferences` (`esphome/core/preferences.h`), used for saving heat pump settings and mode state. Used in `components/cn105/cn105.h`, `components/cn105/heatpump_profile.cpp`, and `components/cn105/heatpump_profile.h`.
- Flash-based NVS/`Preferences` on ESP32 and EEPROM-style storage on ESP8266, both provided by ESPHome.

**File Storage:**
- Local filesystem only (build-time). `secrets.yaml` is read by ESPHome and is git-ignored (`.gitignore` line 1).

**Caching:**
- None. In-memory state only (`components/cn105/cn105.h`, `components/cn105/cycle_management.cpp`).

## Authentication & Identity

**Firmware (ESPHome device):**
- Home Assistant API encryption: `api:` configured with an encryption key from `!secret` (`encryption_key`, `encryption_key_sejour`, `api_key`, `hvac_api` in CI-generated secrets, `.github/workflows/build.yaml`).
- OTA update password: `ota: - platform: esphome` with `password: !secret ota_pwd` (`examples/root-configs/hp-sejour.yaml` lines 83-85).
- Wi-Fi credentials: `wifi:` with `!secret wifi_ssid` / `wifi_password`, plus optional fallback AP with `fallback_password` (`examples/**/wifi*.yaml`).
- Web server / captive portal: `web_server:` and `captive_portal:` in example configs (`examples/dual-setpoint/common/web_server.yaml`).

**Home Assistant custom integration:**
- Config-flow based; no credentials are stored by the integration itself. Authentication to the device is handled by the ESPHome/Home Assistant API layer.
- Implementation: `custom_components/mitsubishi_climate_proxy/config_flow.py` (ConfigFlow and Options flow).

## Monitoring & Observability

**Error Tracking:**
- None (no Sentry or similar detected).

**Logs:**
- ESPHome logger (`logger:` in examples; 10 files). Component logging via `ESP_LOGI`, `ESP_LOGD`, `ESP_LOGW` and the `log_info_uint32` helper (`components/cn105/cn105.cpp` line 326; approximately 285 log call sites across `components/cn105/`).
- Dedicated log tags: `LOG_CONN_TAG` in `components/cn105/cn105.cpp`.
- Diagnostic sensors exposed to Home Assistant: uptime, WiFi signal, ESPHome version, runtime hours, compressor frequency, error codes (`components/cn105/uptime_connection_sensor.cpp`, `components/cn105/runtime_hours_sensor.h`, `components/cn105/compressor_frequency_sensor.h`, `components/cn105/error_code_sensor.h`).
- Home Assistant custom integration logs via Python `logging` (`_LOGGER` in `custom_components/mitsubishi_climate_proxy/__init__.py`).

## CI/CD & Deployment

**Hosting:**
- No server hosting. Firmware is flashed to the ESP device via USB or OTA (ESPHome OTA). The HA integration is installed in Home Assistant, distributed through HACS (`hacs.json`).

**CI Pipeline:**
- GitHub Actions: `.github/workflows/build.yaml`.
  - Job `unit-tests`: CMake configure, build, and `ctest` for `tests/unit`.
  - Job `esphome-build`: depends on `unit-tests`; matrix over 7 example configs (`examples/root-configs/esp32-test`, `lossnay-test`, `hp-debug`, `hp-sejour`, `examples/m5stack-atoms3/atom-m5`, `examples/xiao-esp32s3/heatpump-xiaoESP32S3`, `examples/modular-hvac/hvac-sample`), compiled with `esphome compile`.
  - Secrets: generated at CI time into `secrets.yaml` files with `/dev/urandom`; no repository secrets are required.

## Environment Configuration

**Required env vars:**
- None for the firmware or the HA integration at runtime.
- CI-only environment variables set in `.github/workflows/build.yaml`: `USERNAME`, `PASSWORD` (placeholder values `test` / `github-run`).

**Required secrets (ESPHome `secrets.yaml`, git-ignored):**
- `wifi_ssid`, `wifi_password` (and `wifi_ssid2..4`, `wifi_password2..4` in some configs)
- `ota_pwd`
- `encryption_key`, `encryption_key_sejour`, `api_key`, `hvac_api`
- `mqtt_password`, `broker` (MQTT examples only)
- `fallback_password`, `iot_ssid`, `iot_password`, `iot_domain` (listed in CI secret generation)

**Secrets location:**
- Local `secrets.yaml` next to each ESPHome YAML (or in `examples/root-configs/`, `examples/m5stack-atoms3/`, `examples/xiao-esp32s3/`, `examples/modular-hvac/` per CI copy step). Not committed (`.gitignore` line 1). Contents are not recorded in this document.

## Webhooks & Callbacks

**Incoming:**
- None. The firmware exposes the ESPHome native API (TCP) and optional web server (HTTP); no webhook endpoints are defined in this repository.

**Outgoing:**
- None from the component itself. Outbound traffic is limited to the Home Assistant API connection and optional MQTT publish in example configs.

---

*Integration audit: 2026-10-27*
