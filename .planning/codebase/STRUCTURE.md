---
last_mapped_commit: 5250ba782c261ae98fe54abd2475ae7ea7836475
last_mapped_at: 2026-10-27
---
# Codebase Structure

**Analysis Date:** 2026-10-27

## Directory Layout

```
MitsubishiCN105ESPHome/
├── components/
│   └── cn105/                      # ESPHome external component (firmware runtime + codegen)
├── custom_components/
│   └── mitsubishi_climate_proxy/   # Separate Home Assistant custom integration (Python)
├── examples/                       # Reference ESPHome YAML per hardware / use case
│   ├── root-configs/               # CI-compiled configs (esp32-test, lossnay-test, hp-debug, hp-sejour)
│   ├── dual-setpoint/              # Dual setpoint package layout + common includes
│   ├── m5stack-atoms3/             # M5Stack AtomS3 config + includes
│   ├── modular-hvac/               # Package-based HVAC sample (packages/)
│   └── xiao-esp32s3/               # Seeed XIAO ESP32-S3 config + common includes
├── tests/
│   └── unit/                       # Standalone GoogleTest suite for pure protocol code
│       ├── mocks/                  # Stub ESPHome headers used by the unit build
│       └── test_*.cpp              # One test file per protocol area
├── docs/                           # Documentation images (e.g. level shifter wiring)
├── captures/                       # Screenshots and photos (UI and hardware)
├── .github/workflows/              # CI: build.yaml (unit tests, then ESPHome compile matrix)
├── .vscode/                        # Editor settings (ignored patterns in .gitignore)
├── .planning/codebase/             # Generated codebase map documents (this folder)
├── README.md                       # User documentation (install, wiring, YAML options)
├── hacs.json                       # HACS metadata for the Home Assistant integration
├── icon.svg, logo.svg              # Project branding
└── .gitignore
```

Untracked scratch folders present at analysis time and excluded from this map: `.serena/`. The audit `AUDIT-CN105-2026-10-27.md` was later moved to `.planning/`.

## Directory Purposes

**`components/cn105/`:**
- Purpose: The entire ESPHome external component. Contains the Python codegen, the `CN105Climate` runtime, device profiles, pure protocol code, and per-entity components.
- Contains: `__init__.py` (empty package marker, 0 lines), `climate.py` (Python schema and codegen, 920 lines), about 40 `.h` headers, and about 20 `.cpp` implementation files.
- Key files: `components/cn105/climate.py`, `components/cn105/cn105.h`, `components/cn105/componentEntries.cpp`, `components/cn105/protocol_profile.h`, `components/cn105/heatpump_profile.cpp`, `components/cn105/frame_parser.h`.
- Generated: No. Committed: Yes.

**`custom_components/mitsubishi_climate_proxy/`:**
- Purpose: Home Assistant integration that wraps an ESPHome CN105 climate entity with dual-setpoint, F/C, vane, and coordinator presentation logic.
- Contains: `__init__.py` (entry setup), `climate.py` (1080 lines, `ClimateEntity` wrapper), `config_flow.py` (157 lines), `manifest.json`, `strings.json`, `icons.json`, `translations/en.json`, `brand/` (logo and icon PNGs), `README.md`.
- Key files: `custom_components/mitsubishi_climate_proxy/climate.py`, `custom_components/mitsubishi_climate_proxy/config_flow.py`, `custom_components/mitsubishi_climate_proxy/manifest.json`.
- Generated: No. Committed: Yes.

**`examples/`:**
- Purpose: Working ESPHome YAML for supported boards and layouts. Used by CI as compile targets and by users as templates.
- Contains: YAML files and `includes/` or `common/` fragments. Subfolders have their own `README.md` in `examples/dual-setpoint/`, `examples/xiao-esp32s3/`, `examples/m5stack-atoms3/Readme.md`.
- Key files: `examples/root-configs/hp-debug.yaml`, `examples/root-configs/lossnay-test.yaml`, `examples/xiao-esp32s3/heatpump-xiaoESP32S3.yaml`, `examples/modular-hvac/hvac-sample.yaml`.
- Generated: No. Committed: Yes. Note that `examples/root-configs/.gitignore` and `examples/m5stack-atoms3/.gitignore` exclude local `secrets.yaml` and `.esphome` build output.

**`tests/unit/`:**
- Purpose: Fast host-side regression tests for protocol logic, built without ESPHome.
- Contains: `CMakeLists.txt`, `test_*.cpp` (checksum, lookup tables, temperature, frame parser, profiles, real frames, Lossnay, localization, setpoint persistence, mode preservation, vane persistence, climate action, dual setpoint recenter), `mocks/esphome/` stubs, `mocks/esphome_stubs.h`.
- Key files: `tests/unit/CMakeLists.txt`, `tests/unit/test_profiles.cpp`, `tests/unit/test_frame_parser.cpp`, `tests/unit/mocks/esphome_stubs.h`.
- Generated: Build directory `tests/unit/build/` is created at run time (CI), not committed.
- Committed: Yes (sources only).

**`.github/workflows/`:**
- Purpose: CI pipeline.
- Contains: `build.yaml`. Job `unit-tests` builds `tests/unit` with CMake and runs `ctest`. Job `esphome-build` (depends on `unit-tests`) compiles seven example configs under `examples/root-configs/`, `examples/m5stack-atoms3/`, `examples/xiao-esp32s3/`, and `examples/modular-hvac/` in the `ghcr.io/esphome/esphome:stable` container.
- Committed: Yes.

**`captures/`:**
- Purpose: Screenshots of HA thermostat UI and debug settings, and photos of hardware. Documentation only, not referenced by code.
- Committed: Yes.

**`docs/`:**
- Purpose: Documentation assets. Currently only `docs/cn105-esp32-level-shifter.jpg`, referenced from `README.md`.
- Committed: Yes.

**`.planning/codebase/`:**
- Purpose: Output of the codebase mapper (`ARCHITECTURE.md`, `STRUCTURE.md`, and other focus documents).
- Generated: Yes. Committed: depends on project policy.

## Key File Locations

**Entry Points:**
- `components/cn105/climate.py`: ESPHome platform registration (`CONFIG_SCHEMA`, `to_code`).
- `components/cn105/componentEntries.cpp`: `CN105Climate::setup()` and `CN105Climate::loop()`.
- `components/cn105/climateControls.cpp`: `CN105Climate::control()` and `traits()`, which handle HA and MQTT commands.
- `custom_components/mitsubishi_climate_proxy/__init__.py`: Home Assistant integration entry (`async_setup_entry`).

**Configuration:**
- `components/cn105/climate.py`: Every YAML option name (`CONF_*` constants) and its validation.
- `components/cn105/esphome_compat.h`: ESPHome version shims for `ClimateTraits` and `FixedVector`.
- `components/cn105/Globals.h`: Log tags and shared compile-time constants (`MAX_DATA_BYTES`, `PACKET_LEN`, keep-alive defaults, `CONNECT` and `HEADER` byte sequences).
- `hacs.json`: HACS metadata (name, `render_readme`, `content_in_root: false`).
- `custom_components/mitsubishi_climate_proxy/manifest.json`: HA domain, version, `config_flow: true`.

**Core Logic (firmware):**
- `components/cn105/cn105.h`: `CN105Climate` class declaration, `DriverState` enum, public setters, private state.
- `components/cn105/cn105.cpp`: Constructor, info-request registration, remote-temperature keep-alive, UART setup, reconnect logic.
- `components/cn105/hp_readings.cpp`: UART byte ingestion, command dispatch, publish to ESPHome.
- `components/cn105/hp_writings.cpp`: Packet construction, `writePacket`, pending-write retry, `sendWantedSettings`.
- `components/cn105/heatpump_profile.cpp`: `HeatPumpProfile` (1274 lines), the largest single logic file, covering dual setpoint, vanes, fan, swing, and action.
- `components/cn105/lossnay_profile.cpp`: `LossnayProfile` for heat recovery ventilators.
- `components/cn105/protocol_profile.cpp`: `make_protocol_profile` factory.
- `components/cn105/request_scheduler.cpp`: INFO request queue.
- `components/cn105/cycle_management.cpp`: Polling-cycle state.
- `components/cn105/extraComponents.cpp`: All `set_*` wiring for optional entity pointers.
- `components/cn105/utils.cpp`: General helper implementations (510 lines).
- `components/cn105/heatpumpFunctions.cpp` and `components/cn105/heatpump_functions_protocol.cpp`: Auxiliary functions (code and value) packets.

**Core Logic (protocol, pure):**
- `components/cn105/cn105_protocol.h`: Checksum, temperature encodings, byte-map lookups.
- `components/cn105/cn105_types.h`: Settings and status structs and CONNECT/HEADER constants.
- `components/cn105/frame_parser.h`: Frame assembly.
- `components/cn105/heatpump_profile_protocol.h`: `HeatPumpProtocol` helpers, `SetpointState`.
- `components/cn105/lossnay_profile_protocol.h`: `LossnayProtocol` field masks and mapping tables.
- `components/cn105/localization.h`: Localized label tables.

**Entity components (one per ESPHome entity type):**
- `components/cn105/*_sensor.h` (for example `compressor_frequency_sensor.h`, `input_power_sensor.h`, `kwh_sensor.h`, `runtime_hours_sensor.h`, `outside_air_temperature_sensor.h`, `target_humidity_sensor.h`, `stage_sensor.h`, `sub_mode_sensor.h`, `auto_sub_mode_sensor.h`, `error_code_sensor.h`, `isee_sensor.h`, `functions_sensor.h`, `remote_temp_source_info.h`).
- `components/cn105/van_orientation_select.h`, `components/cn105/hardware_setting_select.h/.cpp`: Select entities.
- `components/cn105/hvac_option_switch.h`: Switches (air purifier, night mode, circulator).
- `components/cn105/functions_button.h`, `components/cn105/functions_number.h`: Auxiliary function buttons and numbers.
- `components/cn105/uptime_connection_sensor.h/.cpp`: Connection uptime sensor.
- `components/cn105/climate_action.h`: Climate action helper.

**Testing:**
- `tests/unit/CMakeLists.txt`: Test target definition. Compiles `components/cn105/heatpump_functions_protocol.cpp` directly, and includes headers from `components/cn105/`.
- `tests/unit/test_*.cpp`: Unit tests.
- `tests/unit/mocks/`: ESPHome stubs for host builds.

**Examples and docs:**
- `examples/root-configs/*.yaml`: Canonical test configs.
- `README.md`: Installation, wiring, and feature documentation.

## Naming Conventions

**Files:**
- Firmware sources mix three styles. Most newer files are `snake_case` (`frame_parser.h`, `request_scheduler.cpp`, `cycle_management.cpp`, `heatpump_profile_protocol.h`). Older files are `camelCase` (`climateControls.cpp`, `componentEntries.cpp`, `heatpumpFunctions.cpp`, `extraComponents.cpp`, `hp_readings.cpp` is snake). Examples: `components/cn105/climateControls.cpp`, `components/cn105/heatpumpFunctions.h`.
- Entity headers end in their role: `*_sensor.h`, `*_select.h/.cpp`, `*_switch.h`, `*_button.h`, `*_number.h`.
- Protocol-only headers end in `_protocol.h` (`cn105_protocol.h`, `heatpump_profile_protocol.h`, `lossnay_profile_protocol.h`, `heatpump_functions_protocol.cpp`, `profile_protocol.h`).
- Profile implementations end in `_profile.h/.cpp` (`heatpump_profile.cpp`, `lossnay_profile.cpp`, `protocol_profile.cpp`).
- Unit tests are `tests/unit/test_<area>.cpp`.
- Python codegen is `climate.py` (singular, one file per ESPHome platform).

**Functions and methods:**
- Class methods on `CN105Climate` mix `camelCase` (`processInput`, `sendWantedSettings`, `buildAndSendInfoPacket`) and `snake_case` for ESPHome-facing setters (`set_tx_rx_pins`, `set_installer_mode`, `set_remote_temp_timeout`). ESPHome codegen uses the `set_*` names.
- Private helpers that touch state end in `_` (`transition_to_`, `maybe_start_connection_`, `restore_setpoint_state_`).
- Pure functions are `snake_case` in the `cn105_protocol` namespace (`checksum`, `decode_temperature`, `encode_msz_a24na_setpoint`).

**Members:**
- Trailing underscore for private members (`state_`, `profile_`, `parser_`, `scheduler_`, `pending_packet_`, `vane_type_`).
- Public ESPHome entity pointers also use trailing underscore (`compressor_frequency_sensor_`, `horizontal_vane_select_`).
- Some public members have no suffix (`functions`, `currentSettings`, `wantedSettings`, `loopCycle`). Use the suffix form for new members.

**Constants:**
- Python YAML keys are `CONF_*` (`CONF_LOSSNAY`, `CONF_RESTORE_SETPOINTS`).
- C++ macros and constants are `UPPER_SNAKE` (`MAX_DATA_BYTES`, `PACKET_LEN`, `DEFAULT_REMOTE_TEMP_KEEPALIVE_INTERVAL_MS`, `HEATPUMP_PROFILE`, `LOSSNAY_PROFILE`).
- Log tags are `static const char*` in `components/cn105/Globals.h` (`TAG`, `LOG_CONN_TAG`, `LOG_SETTINGS_TAG`).

**Namespaces:**
- `esphome` for the component entry (`CN105Climate`, `DriverState`, `RequestScheduler`, `FahrenheitSupport`).
- `esphome::cn105` for profiles and entity helpers (`HeatPumpProfile`, `LossnayProfile`, `Cn105ProtocolProfile`, `HpUpTimeConnectionSensor`).
- `cn105_protocol` for pure protocol code (`FrameParser`, `checksum`, `decode_temperature`).

**Directories:**
- Directory names are lowercase. Most use `snake_case` (`custom_components`, `mitsubishi_climate_proxy`), but `examples/` subfolders such as `root-configs` and `xiao-esp32s3` use hyphens.

## Where to Add New Code

**New heat pump feature or dialect rule (control, setpoint, vane, fan):**
- Primary code: `components/cn105/heatpump_profile.cpp` (declare in `components/cn105/heatpump_profile.h`, and in `components/cn105/protocol_profile.h` if the hook is shared with Lossnay).
- Pure logic to unit test: `components/cn105/heatpump_profile_protocol.h`.
- Tests: `tests/unit/test_<topic>.cpp` and register it in `tests/unit/CMakeLists.txt` `add_executable(cn105_tests ...)`.

**New Lossnay behaviour:**
- Primary code: `components/cn105/lossnay_profile.cpp` and `components/cn105/lossnay_profile.h`.
- Protocol tables and masks: `components/cn105/lossnay_profile_protocol.h`.
- Tests: `tests/unit/test_lossnay.cpp`.

**New INFO request code (for example a new `0x..` polled value):**
- Registration: `components/cn105/cn105.cpp` in `registerInfoRequests()` or `registerHardwareSettingsRequests()`. Use `InfoRequest` from `components/cn105/info_request.h`.
- Response handler: add a `getXxxFromResponsePacket()` method in `components/cn105/hp_readings.cpp` and declare it in `components/cn105/cn105.h`.

**New ESPHome entity (sensor, select, switch, button, number):**
- Implementation: `components/cn105/<name>_sensor.h` (or the matching suffix) following the pattern of `components/cn105/compressor_frequency_sensor.h`.
- YAML wiring: add a `CONF_*` constant, a schema in `components/cn105/climate.py`, and a `cg.add(var.set_...)` line in `to_code`.
- C++ setter and pointer: add `set_xxx` and `xxx_` in `components/cn105/cn105.h`, implement in `components/cn105/extraComponents.cpp`.

**New climate command or traits change:**
- Command handling: `components/cn105/climateControls.cpp` (`controlDelegate`, `process*Change`).
- Advertised modes or features: `components/cn105/climate.py` `supports` schema and `traits()` in `climateControls.cpp`.

**New pure protocol helper:**
- Implementation: `components/cn105/cn105_protocol.h` (general) or the matching `*_protocol.h`. Keep ESPHome includes out of these files so the unit build still compiles.
- Tests: `tests/unit/test_protocol.cpp` or a new `tests/unit/test_<topic>.cpp`.

**New Home Assistant behaviour:**
- Entity logic: `custom_components/mitsubishi_climate_proxy/climate.py`.
- Config flow fields: `custom_components/mitsubishi_climate_proxy/config_flow.py`, with strings in `custom_components/mitsubishi_climate_proxy/strings.json` and `translations/en.json`.

**New example or board:**
- YAML: `examples/<board-or-scenario>/<name>.yaml`. Add to the CI matrix in `.github/workflows/build.yaml` if it should compile in CI.

**Utilities:**
- Shared runtime helpers: `components/cn105/utils.cpp` (declared in the component headers). Pure helpers go to a `*_protocol.h` file or `cn105_protocol.h` instead.

## Special Directories

**`tests/unit/build/`:**
- Purpose: CMake build tree for the unit tests.
- Generated: Yes (by `cmake -B tests/unit/build`).
- Committed: No (by convention; CI creates it).

**`.esphome/` (inside example folders):**
- Purpose: ESPHome build cache for local compiles.
- Generated: Yes. Committed: No (listed in `examples/m5stack-atoms3/.gitignore` and `examples/root-configs/.gitignore`).

**`examples/*/secrets.yaml`:**
- Purpose: Local secrets for ESPHome compile. CI generates a random file (see `.github/workflows/build.yaml`).
- Generated: Yes in CI. Committed: No (ignored by `.gitignore`).

**`.planning/codebase/`:**
- Purpose: Codebase map documents consumed by GSD planning commands.
- Generated: Yes. Committed: project policy.

---

*Structure analysis: 2026-10-27*
