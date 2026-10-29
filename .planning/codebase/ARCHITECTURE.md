---
last_mapped_commit: 5250ba782c261ae98fe54abd2475ae7ea7836475
last_mapped_at: 2026-10-27
---
<!-- refreshed: 2026-10-27 -->
# Architecture

**Analysis Date:** 2026-10-27

## System Overview

```text
┌──────────────────────────────────────────────────────────────────────────┐
│              Home Assistant / MQTT / ESPHome web UI / YAML lambdas        │
│   custom_components/mitsubishi_climate_proxy/  (optional HA proxy layer)  │
└───────────────────────────────┬──────────────────────────────────────────┘
                                │ ESPHome native API / MQTT
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  ESPHome codegen + entity layer  `components/cn105/climate.py`            │
│  (YAML schema → C++ setters; sensors/selects/switches/buttons/numbers)   │
└───────────────────────────────┬──────────────────────────────────────────┘
                                │ generated C++ calls
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  CN105Climate component  `components/cn105/cn105.h` (+ *.cpp per concern) │
│  ┌────────────────┐ ┌──────────────────┐ ┌──────────────────────────────┐ │
│  │ Connection FSM │ │ RequestScheduler │ │ Cn105ProtocolProfile (strategy)│ │
│  │ cn105.cpp      │ │ request_scheduler│ │ HeatPumpProfile | LossnayProfile│ │
│  │ componentEntries│ │ cycle_management │ │ protocol_profile.h            │ │
│  └────────────────┘ └──────────────────┘ └──────────────────────────────┘ │
│  ┌────────────────────┐ ┌────────────────────────┐ ┌────────────────────┐ │
│  │ FrameParser        │ │ Pure protocol helpers  │ │ Writers/readers    │ │
│  │ frame_parser.h     │ │ cn105_protocol.h,      │ │ hp_readings.cpp,   │ │
│  │                    │ │ *_profile_protocol.h   │ │ hp_writings.cpp    │ │
│  └────────────────────┘ └────────────────────────┘ └────────────────────┘ │
└───────────────────────────────┬──────────────────────────────────────────┘
                                │ UARTDevice byte I/O (2400 8E1)
                                ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  Mitsubishi indoor unit / Lossnay via CN105 (level-shifted UART)          │
└──────────────────────────────────────────────────────────────────────────┘
```

Companion pieces outside the firmware:

- `custom_components/mitsubishi_climate_proxy/` is a separate Home Assistant custom integration (`manifest.json` domain `mitsubishi_climate_proxy`, version `2026.5.2`) that wraps an ESPHome CN105 climate entity in a Python `ClimateEntity` (`climate.py`, 1080 lines) with a config flow (`config_flow.py`). It is not loaded by the firmware.
- `tests/unit/` builds a standalone GoogleTest binary that compiles pure protocol sources from `components/cn105/` against stub headers in `tests/unit/mocks/`. No ESPHome runtime is linked.

## Pattern Overview

**Overall:** ESPHome external component (C++ runtime + Python codegen) built around a single-threaded, non-blocking polling state machine, with a Strategy pattern selecting the payload dialect per device type.

**Key Characteristics:**

- Single `CN105Climate` class (`components/cn105/cn105.h`) is the ESPHome `climate::Climate`, `Component`, and `uart::UARTDevice`. It owns transport, scheduling, state publishing, and the connection FSM. It is the composition root for every sub-object.
- Device dialect is chosen at construction: `make_protocol_profile(*this, lossnay)` in `components/cn105/protocol_profile.cpp` returns either `HeatPumpProfile` (`0x30`) or `LossnayProfile` (`0x34`). The profile implements every device-specific decode/encode/control hook declared in `components/cn105/protocol_profile.h`.
- Polling is cooperative: `loop()` drains UART bytes one at a time, feeds them to `FrameParser`, and at most starts one request cycle per `update_interval`. There are no blocking reads or threads on the default path.
- Writes are queued as "wanted" state (`wantedHeatpumpSettings`, `wantedHeatpumpRunStates`) and flushed on a later cycle, so UI commands and remote temperature updates are coalesced rather than sent immediately.
- Mutual exclusion is a `std::mutex` on ESP32 (`wantedSettingsMutex`, `esp32Mutex`) and a `volatile bool` emulation on ESP8266 (`emulateMutex` in `components/cn105/cn105.h`).
- Configuration is declarative YAML validated in Python (`components/cn105/climate.py` `CONFIG_SCHEMA`), then emitted as setter calls (`cg.add(var.set_...)`) in `to_code`.

## Layers

**Configuration / codegen layer (Python):**
- Purpose: Validate YAML, create the C++ object, wire sensors/selects/switches/buttons/numbers and UART pins.
- Location: `components/cn105/climate.py`, `components/cn105/__init__.py` (empty; the package marker for ESPHome external components).
- Contains: schema constants (`CONF_*`), `CONFIG_SCHEMA`, `validate_lossnay_config`, `to_code`, `get_uart_pins_from_config`, `get_uart_port_index`, `_esphome_version_tuple` (ESPHome API-version shim, see issue #733 referenced in code).
- Depends on: ESPHome Python APIs (`esphome.codegen`, `esphome.config_validation`, component modules `climate`, `uart`, `sensor`, `select`, `button`, `switch`, `number`, `binary_sensor`, `text_sensor`, `uptime`).
- Used by: ESPHome compiler when a YAML config contains `climate: - platform: cn105`.

**Entity / public C++ API layer:**
- Purpose: Expose ESPHome climate entity behaviour (`traits()`, `control()`) and setter hooks used by codegen.
- Location: `components/cn105/cn105.h` (class declaration), `components/cn105/climateControls.cpp` (`control`, `controlDelegate`, `traits`, `processModeChange`, `processTemperatureChange`, `processFanChange`, `processSwingChange`), `components/cn105/extraComponents.cpp` (all `set_*_sensor`/`set_*_select`/`set_*_switch` wiring).
- Contains: ESPHome `ClimateCall` translation, traits advertisement, optional entity pointers (null when not configured).
- Depends on: `esphome/components/climate/*`, the optional entity classes in `components/cn105/*.h`.
- Used by: ESPHome Home Assistant API, MQTT, web server, YAML lambdas.

**Connection and lifecycle layer:**
- Purpose: Drive the CN105 handshake and recover from disconnects.
- Location: `components/cn105/cn105.cpp` (`transition_to_`, `setupUART`, `disconnectUART`, `reconnectUART`, `reconnectIfConnectionLost`, `isHeatpumpConnectionActive`, `force_low_level_uart_reinit`), `components/cn105/componentEntries.cpp` (`setup`, `loop`, `maybe_start_connection_`, `set_update_interval`), `components/cn105/hp_readings.cpp` (`handleConnectionSuccess`).
- Contains: `DriverState` enum (`BOOT`, `WAIT_WIFI`, `WAIT_GRACE`, `CONNECTING`, `CONNECTED`, `DISCONNECTED`) declared in `components/cn105/cn105.h`. Transitions are logged under the `FSM` tag.
- Depends on: ESP-IDF UART driver (`driver/uart.h`, guarded by `USE_ESP32`) for low-level reinit.
- Used by: `loop()` gates all write cycles on `isHeatpumpConnected()`.

**Polling and scheduling layer:**
- Purpose: Decide which INFO codes to request in each cycle and time out stuck requests.
- Location: `components/cn105/request_scheduler.h/.cpp` (`RequestScheduler`), `components/cn105/info_request.h` (`InfoRequest` struct), `components/cn105/cycle_management.h/.cpp` (`cycleManagement` struct), `components/cn105/cn105.cpp` (`registerInfoRequests`, `registerHardwareSettingsRequests`), `components/cn105/hp_writings.cpp` (`buildAndSendRequestsInfoPackets`, `buildAndSendInfoPacket`).
- Contains: registered requests keyed by code (`0x02` settings, `0x03` room temp, `0x06` status, plus functions/timers/standby/hvac-option codes), per-request `canSend` and `onResponse` callbacks, soft and hard timeouts via ESPHome `set_timeout`.
- Depends on: callbacks injected from `CN105Climate` in its constructor (`cn105.cpp` lines 31–45). The scheduler never includes `CN105Climate` directly beyond a forward declaration.
- Used by: `loop()` via `buildAndSendRequestsInfoPackets()` when `loopCycle.hasUpdateIntervalPassed(...)`.

**Frame and protocol layer (pure):**
- Purpose: Parse byte streams and encode or decode CN105 frames without ESPHome dependencies.
- Location: `components/cn105/frame_parser.h` (`cn105_protocol::FrameParser`), `components/cn105/cn105_protocol.h` (checksum `(0xFC - sum) & 0xFF`, temperature encode/decode, byte-map lookups), `components/cn105/cn105_types.h` (`heatpumpSettings`, `wantedHeatpumpSettings`, `heatpumpStatus`, `heatpumpRunStates`), `components/cn105/heatpump_profile_protocol.h` (`HeatPumpProtocol`, `SetpointState` persistence and deadband helpers), `components/cn105/lossnay_profile_protocol.h` (`LossnayProtocol`, `LOSSNAY_PROFILE = 0x34`), `components/cn105/heatpumpFunctions.h` plus `heatpump_functions_protocol.cpp`, `components/cn105/localization.h`.
- Depends on: `<cstdint>`, `<cmath>`, `<cstring>`, `<optional>`; `heatpump_profile_protocol.h` also uses `esphome/components/climate/climate_mode.h`.
- Used by: `CN105Climate::processInput`, the profiles, and the unit tests (`tests/unit/test_*.cpp`).

**Device profile (strategy) layer:**
- Purpose: Implement dialect-specific decoding, encoding, and control policy.
- Location: Base interface `components/cn105/protocol_profile.h` (`Cn105ProtocolProfile`, `ProfileFeature` enum). Implementations `components/cn105/heatpump_profile.h/.cpp` (`HeatPumpProfile`, 1274-line implementation, dual-setpoint, vane, and remote temperature logic) and `components/cn105/lossnay_profile.h/.cpp` (`LossnayProfile`, 114 lines). Shared default `Cn105ProtocolProfile::decode_room_status` lives in `components/cn105/protocol_profile.cpp`.
- Depends on: `CN105Climate&` (held as `hp_`, and `friend class` access declared in `cn105.h`), so profiles mutate the climate's `currentSettings`, `wantedSettings`, and entity pointers directly.
- Used by: `CN105Climate::setup`, `processCommand`, `control`, `loop` through `profile_->...` calls.

**Read/publish and write path:**
- Purpose: Turn decoded frames into ESPHome state and turn wanted state into outbound frames.
- Location: Reads `components/cn105/hp_readings.cpp` (`processInput`, `processDataPacket`, `getDataFromResponsePacket`, `getSettingsFromResponsePacket`, `getRoomTemperatureFromResponsePacket`, `statusChanged`, `publishStateToHA`, `heatpumpUpdate`, `terminateCycle`). Writes `components/cn105/hp_writings.cpp` (`prepareInfoPacket`, `prepareSetPacket`, `writePacket`, `try_write_pending_packet`, `sendWantedSettings`, `sendRemoteTemperature`).
- Depends on: `FrameParser`, the profile, the UART `write_byte` API.
- Used by: `loop()` and `control()`.

**Optional entity layer (ESPHome components per sensor/control):**
- Purpose: Expose diagnostics and vane/option controls as separate ESPHome entities that the climate writes to.
- Location: One header per entity in `components/cn105/`: `compressor_frequency_sensor.h`, `input_power_sensor.h`, `kwh_sensor.h`, `runtime_hours_sensor.h`, `outside_air_temperature_sensor.h`, `target_humidity_sensor.h`, `isee_sensor.h`, `stage_sensor.h`, `sub_mode_sensor.h`, `auto_sub_mode_sensor.h`, `error_code_sensor.h`, `remote_temp_source_info.h`, `uptime_connection_sensor.h/.cpp`, `van_orientation_select.h`, `hardware_setting_select.h/.cpp`, `hvac_option_switch.h`, `functions_button.h`, `functions_number.h`, `functions_sensor.h`, `climate_action.h`.
- Depends on: ESPHome `sensor`, `select`, `switch`, `button`, `number`, `binary_sensor`, `text_sensor` component bases.
- Used by: pointers stored on `CN105Climate` and updated from profile decoding (`updateExtraSelectComponents` in `heatpump_profile.cpp`).

**Home Assistant proxy layer (separate package):**
- Purpose: Present the CN105 climate with Home Assistant-specific behaviour (dual setpoint presentation, F/C mapping, horizontal and vertical vane entities, optional single-target coordinator mode, restore last mode).
- Location: `custom_components/mitsubishi_climate_proxy/climate.py`, `custom_components/mitsubishi_climate_proxy/config_flow.py`, `custom_components/mitsubishi_climate_proxy/__init__.py` (`async_setup_entry` forwards to the `climate` platform).
- Depends on: `homeassistant.components.climate`, `homeassistant.helpers.event`, `homeassistant.helpers.restore_state`. Wraps an existing ESPHome climate entity id via `CONF_SOURCE_ENTITY`.
- Used by: Home Assistant users who install the integration; not required for firmware operation.

## Data Flow

### Primary Request Path (polling read)

1. ESPHome calls `CN105Climate::loop()` each main-loop tick (`components/cn105/componentEntries.cpp`).
2. `maybe_start_connection_()` runs the UART/CONNECT bootstrap after `conn_bootstrap_delay_ms_` (default 10 s) (`components/cn105/componentEntries.cpp`).
3. `processInput()` reads every available byte from `get_hw_serial_()` and calls `parser_.feed()` (`components/cn105/hp_readings.cpp`, `components/cn105/frame_parser.h`).
4. When `parser_.frame_complete()` is true, `processDataPacket()` validates the checksum and dispatches on command byte through `processCommand()` (`components/cn105/hp_readings.cpp`).
5. `0x62` data frames go to `getDataFromResponsePacket()`, which invokes the matching `InfoRequest::onResponse` (registered in `registerInfoRequests()` in `components/cn105/cn105.cpp`), e.g. `getSettingsFromResponsePacket()`.
6. The profile decodes the payload (`profile_->decode_settings()`, `decode_status()`, `decode_room_status()`) and `heatpumpUpdate()` / `statusChanged()` publish via `publish_state()` on the ESPHome climate and entity pointers (`components/cn105/hp_readings.cpp`).
7. `0x61` ACK triggers `updateSuccess()`; the scheduler then advances, and when no requests remain `terminateCycle()` calls `loopCycle.cycleEnded()` and optionally `sendRemoteTemperature()` (`components/cn105/hp_readings.cpp`).

### Command Path (user changes a setting)

1. HA or MQTT calls `CN105Climate::control(const ClimateCall&)` (`components/cn105/climateControls.cpp`).
2. `control()` runs `controlDelegate()` under `emulateMutex("CONTROL_WANTED_SETTINGS", ...)`, which dispatches to `processModeChange`, `processTemperatureChange`, `processFanChange`, `processSwingChange`, and then `finalizeControlIfUpdated()`.
3. Changes land in `wantedSettings` / `wantedRunStates` with `hasChanged` set. The profile's `process_temperature_change()` and `control_*()` methods apply dialect rules (`components/cn105/heatpump_profile.cpp`).
4. On a later `loop()` tick with no cycle running, `checkPendingWantedSettings()` or `checkPendingWantedRunStates()` runs, which calls `sendWantedSettings()` → `prepareSetPacket()` → `writePacket()` (`components/cn105/hp_writings.cpp`).
5. `writePacket()` writes bytes via `write_byte()`. If the UART is not connected it stores the packet in `pending_packet_` and retries via `set_timeout("write", 4000, ...)` (`components/cn105/hp_writings.cpp`).

### Remote Temperature Path

1. A YAML-configured sensor or service calls `set_remote_temperature(float)` (`components/cn105/climateControls.cpp`).
2. `startRemoteTempKeepAlive()` (`components/cn105/cn105.cpp`) re-sends the value on a timer (`remote_temp_keepalive_interval_ms_`, default 20 000 ms) to prevent fallback to the indoor unit sensor. `set_remote_temp_timeout` and `pingExternalTemperature` handle expiry.
3. Sends use `sendRemoteTemperature()` / `sendRemoteTemperaturePacket()` with debounce (`debounce_delay_`) and change detection (`last_remote_temp_sent_`).

### Connection Lifecycle

1. `setup()` restores persisted HEAT_COOL band (opt-in, `restore_setpoint_state_()`), registers requests, and does NOT touch UART (`components/cn105/componentEntries.cpp`).
2. First `loop()` after `conn_bootstrap_delay_ms_` calls `setupUART()` and sends CONNECT (`0x5A` standard, or `0x5B` installer mode), transitioning `BOOT → CONNECTING`.
3. The heat pump answers `0x7A`/`0x7B`; `handleConnectionSuccess()` (via `profile_->accepts_handshake`) transitions to `CONNECTED`.
4. A response timeout moves the FSM to `DISCONNECTED`; `reconnectIfConnectionLost()` / `reconnectUART()` re-run UART setup and CONNECT. Installer-mode fallback to standard handshake is handled by `installer_mode_fallback_done_`.

**State Management:**
- Device state lives in value structs on the climate object: `currentSettings`, `wantedSettings`, `currentStatus`, `currentRunStates`, `wantedRunStates`, `functions` (`components/cn105/cn105.h`).
- Cycle state is `loopCycle` (`cycleManagement`) and scheduler state is `scheduler_` (`RequestScheduler`).
- Persistent state uses ESPHome preferences: `setpoint_pref_` (`ESPPreferenceObject`) in `HeatPumpProfile` (`components/cn105/heatpump_profile.h`).

## Key Abstractions

**CN105Climate:**
- Purpose: The single ESPHome climate entity and orchestrator for one CN105 port.
- Examples: `components/cn105/cn105.h`, `components/cn105/cn105.cpp`, `components/cn105/componentEntries.cpp`, `components/cn105/hp_readings.cpp`, `components/cn105/hp_writings.cpp`, `components/cn105/climateControls.cpp`, `components/cn105/extraComponents.cpp`, `components/cn105/heatpumpFunctions.cpp`
- Pattern: Partial-class-by-concern. One class is split across multiple `.cpp` files grouped by responsibility. There is no separate facade.

**Cn105ProtocolProfile:**
- Purpose: Strategy interface for device dialects. Hooks cover settings decode, status decode, control encode, fan and temperature policy, swing policy, and feature gating via `supports()` and `allow_operation()`.
- Examples: `components/cn105/protocol_profile.h`, `components/cn105/heatpump_profile.h`, `components/cn105/lossnay_profile.h`
- Pattern: Virtual strategy with a factory (`make_protocol_profile`). Feature gating uses `ProfileFeature` enum values (`TEMPERATURE`, `SWING`, `REMOTE_TEMPERATURE`, `AUXILIARY_CONTROLS`, `INSTALLER_MODE`).

**RequestScheduler and InfoRequest:**
- Purpose: Round-robin queue of INFO requests with per-request interval, failure counting, and optional `canSend`/`onResponse` hooks.
- Examples: `components/cn105/request_scheduler.h`, `components/cn105/request_scheduler.cpp`, `components/cn105/info_request.h`
- Pattern: Command queue with callbacks injected via `std::function` from the owning climate (`cn105.cpp` constructor).

**FrameParser:**
- Purpose: Byte-by-byte assembly of CN105 frames (`0xFC` start, command, length, payload, checksum), with overflow and sanity checks.
- Examples: `components/cn105/frame_parser.h`
- Pattern: Stateful push parser with no ESPHome dependency, tested in `tests/unit/test_frame_parser.cpp`.

**cycleManagement:**
- Purpose: Tracks whether a polling cycle is running, when the last cycle started/completed, and timeout detection.
- Examples: `components/cn105/cycle_management.h`, `components/cn105/cycle_management.cpp`
- Pattern: Plain struct with imperative methods; `deferCycle()` reschedules writes when a cycle is in flight.

**Pure protocol helpers:**
- Purpose: Testable encode/decode and lookup functions in the `cn105_protocol` namespace.
- Examples: `components/cn105/cn105_protocol.h`, `components/cn105/heatpump_profile_protocol.h`, `components/cn105/lossnay_profile_protocol.h`
- Pattern: Header-only `inline` functions and `static` tables, with no ESPHome includes on the protocol-only path.

**Optional entity components:**
- Purpose: Diagnostic sensors, vane selects, option switches, and function buttons/numbers as independent ESPHome components referenced by pointer.
- Examples: `components/cn105/compressor_frequency_sensor.h`, `components/cn105/van_orientation_select.h`, `components/cn105/hvac_option_switch.h`, `components/cn105/functions_button.h`
- Pattern: Each extends an ESPHome base (`sensor::Sensor`, `select::Select`, etc.) and is null-checked before use in the climate.

## Entry Points

**ESPHome component registration (firmware):**
- Location: `components/cn105/climate.py` (`CONFIG_SCHEMA`, `to_code`), plus `components/cn105/__init__.py` as the package marker.
- Triggers: ESPHome compile of any YAML containing `climate: - platform: cn105`. Example configurations in `examples/root-configs/*.yaml`, `examples/xiao-esp32s3/heatpump-xiaoESP32S3.yaml`, `examples/m5stack-atoms3/atom-m5.yaml`, `examples/modular-hvac/hvac-sample.yaml`.
- Responsibilities: Validate config, instantiate `CN105Climate` with the UART component, emit setter calls.

**Firmware runtime callbacks:**
- Location: `components/cn105/componentEntries.cpp` (`setup`, `loop`), `components/cn105/climateControls.cpp` (`control`, `traits`).
- Triggers: ESPHome main loop and climate command from the API or MQTT.
- Responsibilities: Non-blocking UART drain, cycle start, command queueing.

**UART ingress:**
- Location: `components/cn105/hp_readings.cpp` (`processInput`, `processDataPacket`).
- Triggers: Every byte received on the configured UART (`tx_pin`/`rx_pin` set via `set_tx_rx_pins`).
- Responsibilities: Frame assembly, checksum, dispatch.

**Home Assistant integration:**
- Location: `custom_components/mitsubishi_climate_proxy/__init__.py` (`async_setup_entry`), `custom_components/mitsubishi_climate_proxy/config_flow.py`.
- Triggers: Home Assistant config entry load.
- Responsibilities: Forward to the `climate` platform, reload on options change.

**Unit test entry point:**
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

**What happens:** `CN105Climate` declares well over 100 member functions and roughly 80 data members in `components/cn105/cn105.h` (535 lines), and its implementation is split over `cn105.cpp`, `componentEntries.cpp`, `climateControls.cpp`, `hp_readings.cpp`, `hp_writings.cpp`, `extraComponents.cpp`, `heatpumpFunctions.cpp`, and `hardware_setting_select.cpp`.
**Why it's wrong:** Any new feature touches the header, which forces recompilation of every entity header and profile. The `public:` surface mixes configuration setters, state accessors, and internal helpers. Several members are legacy or unused (`isReading` and `isWriting` appear only in their declarations; `horizontal_vanes_` is kept "for legacy logging"; `uart_setup_switch` is declared as a public bool).
**Do this instead:** Add new logic to the existing profile (`components/cn105/heatpump_profile.cpp` or `components/cn105/lossnay_profile.cpp`) when it is dialect-specific, or to a pure helper in a `*_protocol.h` file when it can be unit-tested. Keep new members out of `cn105.h` where possible.

### Duplicate or stale protocol-state flags

**What happens:** `cn105.h` contains a comment "Legacy booleans replaced by DriverState FSM" but also keeps `isUARTReady_()` and `isHeatpumpConnected()` as derived accessors. `wantedSettingsMutex` is a `std::mutex` on ESP32 and a `volatile bool` on ESP8266 with different semantics.
**Why it's wrong:** Two sources of truth for connection state invite race bugs on the ESP8266 path, which lacks a real mutex.
**Do this instead:** Read connection state only through `driver_state()` / `isHeatpumpConnected()`, and wrap any new shared flags in the existing `emulateMutex` helper.

### Writing raw protocol bytes outside the write path

**What happens:** Packet construction is split across `hp_writings.cpp` (`prepareSetPacket`, `prepareInfoPacket`, `createPacket`) and profile `encode_control`/`encode_run_states` methods in `heatpump_profile.cpp`.
**Why it's wrong:** A new command must be encoded in the profile and also routed through `writePacket`, which skips the `checkIsActive` guard when `false` is passed.
**Do this instead:** Encode via the profile `encode_*` hooks and always send through `writePacket(packet, length)` with the default `checkIsActive = true` unless the path is the CONNECT or handshake packet.

### Mojibake comments in source

**What happens:** Several files contain double-encoded UTF-8 comments (for example in `components/cn105/cn105.h` and `components/cn105/climate.py`). These are not functional but make the code harder to review.
**Why it's wrong:** Reviewers cannot read the intent of the affected comments, and tools may corrupt them further.
**Do this instead:** Rewrite these comments in plain ASCII or UTF-8 when touching the file.

## Error Handling

**Strategy:** Log and degrade. Protocol errors are logged and the connection FSM falls back to `DISCONNECTED` for reconnection, rather than raising. Unknown bytes are logged once (`log_unknown_status_byte_once` in `components/cn105/heatpump_profile.cpp`) and the last known value is kept.

**Patterns:**
- Checksum mismatch and malformed frames are dropped by `FrameParser` (`components/cn105/frame_parser.h`) and `processDataPacket` (`components/cn105/hp_readings.cpp`).
- Writes while disconnected are queued in `pending_packet_` and retried after 4 s (`components/cn105/hp_writings.cpp`). Packets larger than `PACKET_LEN` (22) are dropped with an error.
- Request timeouts increment `InfoRequest::failures`; once `maxFailures` is reached the request is disabled (`components/cn105/info_request.h`).
- Cycle timeouts force `cycleEnded(true)` (`components/cn105/cycle_management.cpp`, `checkTimeout`).

## Cross-Cutting Concerns

**Logging:** ESPHome `ESP_LOGx` macros with module tags defined in `components/cn105/Globals.h` (`CN105`, `FSM`, `Decoder`, `CN105_CONN`, `CYCLE`, `SETTINGS`, `STATUS`, `REMOTE_TEMP`, and others). Verbose frame dumps go through `hpPacketDebug` on `Decoder` / `WRITE` / `READ` tags.

**Validation:** YAML is validated in `components/cn105/climate.py` (`CONFIG_SCHEMA`, `validate_lossnay_config`, `SELECT_SCHEMA`, `*_SENSOR_SCHEMA`). Runtime values are range-checked by protocol helpers (`encode_msz_a24na_setpoint` clamps to 16.0 to 31.0 °C in `components/cn105/cn105_protocol.h`).

**Authentication:** Not applicable to the firmware. Home Assistant API and OTA security are configured in user YAML (`api`, `ota`) and not in this repository's component code. The HA proxy uses Home Assistant's own auth.

**Persistence:** ESPHome preferences via `ESPPreferenceObject` for HEAT_COOL setpoint band (`components/cn105/heatpump_profile.h`, `save_setpoints` / `restore_setpoints` in `components/cn105/heatpump_profile.cpp`). Opt-in via `supports.restore_setpoints`.

---

*Architecture analysis: 2026-10-27*
