---
last_mapped_commit: 5250ba782c261ae98fe54abd2475ae7ea7836475
last_mapped_at: 2026-10-27
---
# Codebase Concerns

**Analysis Date:** 2026-10-27

Scope: full repository. Main code under analysis is the ESPHome component `components/cn105/` (about 8,100 lines of C++ and Python), the HA integration `custom_components/mitsubishi_climate_proxy/`, the unit tests in `tests/unit/`, and the CI workflow `.github/workflows/build.yaml`.

Verification note: `AUDIT-CN105-2026-10-27.md` (untracked, repo root) was used as an input. Each item below was re-checked against the code. Items marked **[verified in code]** were confirmed by reading the cited lines. Items marked **[verified by probe]** were also reproduced with a small program. Items marked **[audit only]** were not re-checked on hardware or in a running build.

## Tech Debt

**Global clock macro and unsigned time arithmetic (cross-cutting):**
- Issue: Every timing check calls the `CUSTOM_MILLIS` macro directly. The code mixes `unsigned long`, `uint32_t`, and `long` for timestamps and differences. Some helpers assume monotonic time without wrap handling.
- Files: `components/cn105/Globals.h`, `components/cn105/cycle_management.cpp`, `components/cn105/cn105.cpp` (lines ~374-410), `components/cn105/climateControls.cpp` (lines 19, 39), `components/cn105/componentEntries.cpp` (line 158), `components/cn105/heatpump_profile.cpp` (lines 457-485)
- Impact: The clock cannot be injected or mocked, so wrap-around and timing behaviour cannot be unit-tested. This is the root cause of BUG-01 below. It also blocks regression tests for the grace-window logic.
- Fix approach: Introduce an injectable clock (a `now()` function pointer or small interface in `*_protocol.h` style) and route all timing through it. Use `uint32_t` for timestamps and differences, and compare with `(int32_t)(now - t)` where a deadline is stored.

**Public surface and friend coupling in `CN105Climate`:**
- Issue: `components/cn105/cn105.h` has a `public:` section with roughly 30 `const char*` state members. Profiles reach into internals through `friend class cn105::HeatPumpProfile` and `friend class cn105::LossnayProfile` (lines 392-393). The profile refactor is incomplete: `heatpump_profile.cpp` (1,274 lines) still writes `hp_.wantedSettings.*` and `hp_.data` directly.
- Files: `components/cn105/cn105.h`, `components/cn105/heatpump_profile.cpp`, `components/cn105/lossnay_profile.cpp`, `components/cn105/heatpump_profile_protocol.h`
- Impact: Any change to a field can break both the main class and every profile. Refactors need to touch many files at once.
- Fix approach: Finish moving state into the profile objects. Expose only the methods the profiles need, and keep the protocol logic pure in `*_protocol.h`.

**Stringly-typed state:**
- Issue: Mode, power, fan, vane and wide-vane are `const char*` values compared with `strcmp`. `cn105.h` and `cn105_types.h` contain about 68 `const char*` declarations in total. Null checks are scattered (for example `heatpump_profile.cpp:318-332` checks `wantedSettings.mode == nullptr`).
- Files: `components/cn105/cn105_types.h`, `components/cn105/cn105.h`, `components/cn105/heatpump_profile.cpp`, `components/cn105/hp_writings.cpp` (lines 138-176), `components/cn105/climateControls.cpp` (lines 205-219)
- Impact: A missing null check or a typo in a literal goes unnoticed at compile time. This is a recurring source of null-pointer bugs.
- Fix approach: Replace with `enum class` types, convert to text only at the Home Assistant publish boundary.

**Unnecessary mutex and `emulateMutex` on ESP8266 (verified in code):**
- Issue: The ESPHome main loop runs on one thread, so `std::mutex wantedSettingsMutex` (`cn105.h:456`, locked at `hp_writings.cpp:262` and `climateControls.cpp:141`) gives no protection on ESP32. On ESP8266 the code uses `volatile bool wantedSettingsMutex` (`cn105.h:458`) with `emulateMutex` (`utils.cpp:385-416`), which defers work with `set_timeout` and forcibly runs the callback after 10 retries even if the "lock" is still held.
- Files: `components/cn105/hp_writings.cpp:257-266`, `components/cn105/climateControls.cpp:141`, `components/cn105/utils.cpp:385-451`, `components/cn105/cn105.h:456-458`
- Impact: Extra code paths with no benefit. The forced unlock path is a silent race in principle, and the logs call it a "forcing unlock" without any recovery. The `esp8266Mutex` copy in the `TEST_MODE` block (`utils.cpp:419-451`) duplicates the logic.
- Fix approach: Remove both mutex types. Use a simple "pending" state flag, and make `sendWantedSettings` synchronous.

**Dead code, TODOs and debug leftovers:**
- Issue: Several blocks are commented out or unused: `buildAndSendRequestPacket` (historical path), the empty `RequestScheduler::loop()`, commented-out disconnect logic in `isHeatpumpConnectionActive` (`cn105.cpp`), `//friend class VaneOrientationSelect;` (`cn105.h:63`), and a commented `ESP_LOGI` in `cycle_management.cpp:25`. `TEST_MODE` guards appear in several files (for example `utils.cpp:417`).
- Files: `components/cn105/cn105.h`, `components/cn105/cn105.cpp`, `components/cn105/request_scheduler.cpp`, `components/cn105/utils.cpp`
- Impact: Noise when reading the code. The commented-out disconnect logic in `isHeatpumpConnectionActive` is easy to re-enable by mistake.
- Fix approach: Delete dead code. Keep behaviour in git history.

**Encoding and language mix:**
- Issue: Several files contain mojibake (double-encoded UTF-8), for example `components/cn105/climate.py` line 57 and the comments in `get_uart_port_index` (`climate.py:~270-285`). Logs and comments mix French and English (for example `hp_writings.cpp:12-33`, `connect` log lines in `hp_writings.cpp:24-26`).
- Files: `components/cn105/climate.py`, `components/cn105/cn105.h`, `components/cn105/hp_readings.cpp`, `components/cn105/hp_writings.cpp`
- Impact: Hard to read and search. Log strings are part of the user-facing diagnostics, so mixed languages are confusing for non-French users.
- Fix approach: Normalise all sources to UTF-8, and write logs and comments in English.

**Test and CI coverage is narrow:**
- Issue: CI (`.github/workflows/build.yaml`) runs unit tests on `ubuntu-latest` and compiles only `stable` ESPHome (`matrix.version: [stable]`). No `beta` job, no compiler warnings as errors, no sanitizers. The unit test CMake (`tests/unit/CMakeLists.txt`) has no `-Wall -Wextra -Werror`.
- Files: `.github/workflows/build.yaml`, `tests/unit/CMakeLists.txt`
- Impact: ESPHome API breaks (for example the MAC address handling change in commit `3d0adee`) are only caught after they reach `stable`. Warnings in the C++ code are not enforced.
- Fix approach: Add an ESPHome `beta` job with `continue-on-error: true`. Add `-Wall -Wextra -Werror` to the test target. Consider ASan/UBSan for `ctest`.

## Known Bugs

**`millis()` wrap-around freezes the polling cycle (BUG-01) [verified in code]:**
- Symptoms: After about 49.7 days of uptime, `millis()` wraps to zero. Home Assistant then keeps stale values and no new polling cycle starts. Recovery only happens after a user command triggers a reconnect.
- Files: `components/cn105/cycle_management.cpp` (`hasUpdateIntervalPassed` line 58, `doesCycleTimeOut` line 63, `deferCycle` line 35, `cycleEnded` line 47)
- Trigger: `deferCycle()` stores `lastCompleteCycleMs = CUSTOM_MILLIS + delay`. The guard `if (CUSTOM_MILLIS < lastCompleteCycleMs) return false;` then returns `false` until the clock catches up with that future deadline. After a wrap, `now` is smaller than the stored deadline, so the guard fires for roughly another 49 days.
- Workaround: Reboot the ESP. Sending a user command also triggers a reconnect that clears the state.
- Fix approach: Store the deferral as `defer_until_ms` and test it with a signed difference, `(int32_t)(now - defer_until_ms) >= 0`. Use `(now - last) >= interval` on unsigned values for the interval test. Add unit tests that inject values around the wrap point. The audit proposed the same fix. Time to trigger is about 49.7 days, so severity depends on how long devices stay powered.

**`abs()` on a float returns an integer (BUG-02) [verified by probe]:**
- Symptoms: The "remote temperature control" binary sensor uses a margin of about 1 °C instead of `remote_temperature_margin` (default 0.4 °C).
- Files: `components/cn105/hp_readings.cpp:84`
- Trigger: `abs(receivedStatus.roomTemperature - this->remoteTemperature_)` resolves to the integer `abs`. A probe with `abs(21.0f - 20.7f)` printed `0.000000`, so any difference below 1 °C reads as 0 and is treated as "remote".
- Workaround: None in config.
- Fix approach: Use `std::fabs(...)`. Add a unit test where the difference is 0.6 °C and the margin is 0.4 °C, expecting "internal". The same pattern does not appear in `localization.h` (which uses `std::abs` correctly) or `cn105_protocol.h:258`.

**Commands are published optimistically and never confirmed (BUG-03) [verified in code]:**
- Symptoms: Home Assistant shows the requested state immediately. If the heat pump never applies the command, the UI keeps the wrong state until the next status read, and grace windows delay the correction further.
- Files: `components/cn105/hp_writings.cpp:225-245` (`sendWantedSettingsDelegate`), `components/cn105/hp_readings.cpp:168-180` (`updateSuccess` is an empty stub for ACK `0x61`)
- Trigger: `sendWantedSettingsDelegate` sets `hasBeenSent = true`, calls `publishWantedSettingsStateToHA()`, and then calls `wantedSettings.resetSettings()` right after `writePacket()`. `writePacket()` may only queue the packet (see BUG-04). The queued bytes are kept, so the command is not lost unless the single pending slot is overwritten. No ACK is tracked, and nothing retries.
- Workaround: Re-send the setting from HA.
- Fix approach: Track an `in_flight` command with a timestamp. Mark it applied on ACK `0x61` or when a `0x02` status read is consistent with it. Retry N times, then log a warning and republish the real state. Put this logic in a pure function so it can be tested.

**Single pending-packet slot, overwritten by info requests (BUG-04) [verified in code]:**
- Symptoms: A user command waiting for reconnection can be silently replaced by an info request.
- Files: `components/cn105/hp_writings.cpp:96-136` (`writePacket`, `try_write_pending_packet`), `pending_packet_` in `components/cn105/cn105.h`
- Trigger: Every failed write calls `reconnectUART()` (line 113). The code then stores the new packet in `pending_packet_` with no check for an existing pending user command. An info request issued during the same window overwrites it. Each failed write also restarts a 4-second timeout, so repeated failures produce a burst of reconnects.
- Workaround: None.
- Fix approach: Never queue info requests (they are replayed by the cycle). Give user commands priority. Allow only one reconnect in flight, using the driver state machine as the guard.

**Reconnect storm guard is not enforced in `writePacket` (related to BUG-04) [verified in code]:**
- Symptoms: Repeated "could not write as asked" logs followed by reconnect attempts.
- Files: `components/cn105/hp_writings.cpp:96-120`, `components/cn105/cn105.cpp:384-392` (`reconnectIfConnectionLost` checks `lastReconnectTimeMs`, but `writePacket` does not)
- Trigger: `writePacket()` calls `reconnectUART()` without checking `lastReconnectTimeMs`.
- Fix approach: Gate reconnects on the driver state and the `update_interval_` window in one place.

**UART reset may target the wrong port (BUG-05) [audit only, code confirmed]:**
- Symptoms: The low-level re-initialisation may reconfigure the logger UART (usually UART0) and leave the CN105 UART untouched.
- Files: `components/cn105/climate.py` (`get_uart_port_index`, around line 270-285), `components/cn105/cn105.cpp` (`force_low_level_uart_reinit`, around line 416-491, called from `reconnectUART` at line 378)
- Trigger: The port index is inferred from the order of `uart:` declarations in YAML (first entry becomes index 0), while ESPHome chooses the hardware port itself. Unknown pins fall back to GPIO1/2. The comment in `reconnectUART` says "Disabled", but `force_low_level_uart_reinit()` is still called on the next line.
- Workaround: Use the default `UARTComponent` reset only. The code path cannot be verified without an ESP32 and a serial logger.
- Fix approach: Read the real port from `IDFUARTComponent`, or remove the low-level reset if it adds nothing. Never fall back to UART0 or GPIO1/2. Verify on hardware before changing.

**HEAT_COOL setpoint is not recalculated as room temperature changes (BUG-06) [audit only]:**
- Symptoms: The heat pump regulates on the setpoint computed when the user last changed it, not on the low/high range.
- Files: `components/cn105/heatpump_profile.cpp:636-666`, `components/cn105/heatpump_profile_protocol.h` (`deadband_setting`)
- Workaround: Set the temperature again after the room temperature changes.
- Fix approach: Recompute the setpoint on each room temperature change, with hysteresis and a minimum send interval. See FEAT-03 in the audit.

**Polling cycle can stall on missing or late responses (BUG-07) [audit only, consistent with code]:**
- Symptoms: One missing reply holds the cycle until `2 × update_interval + 1 s` has passed.
- Files: `components/cn105/cn105.cpp:94-137` (`registerInfoRequests`, requests 0x02, 0x03, 0x04, 0x06 with `soft_timeout_ms = 0`), `components/cn105/request_scheduler.cpp`
- Trigger: Late responses for a code other than the current request re-enter `send_next_after`, which can duplicate the request chain.
- Fix approach: Use a short soft timeout for every request. In `process_response`, ignore or consume responses that do not match `current_request_index_`.

**Energy counter is 16-bit (BUG-09) [verified in code, TODO present]:**
- Symptoms: The kWh value wraps or saturates at 6553.5 kWh.
- Files: `components/cn105/heatpump_profile.cpp:181-198` (the TODO at line 181 says the counter size is unknown)
- Trigger: Cumulative energy above 6553.5 kWh (about 6.5 MWh) on a unit that reports two bytes.
- Fix approach: Either detect wrap-around and accumulate (with optional persistence), or publish with `state_class: total_increasing` and document the limit.

## Security Considerations

**Encryption and OTA secrets are referenced from `secrets.yaml` (verified):**
- Risk: If a user copies an example and puts literal values into it, the API key and OTA password are committed to Git.
- Files: `examples/dual-setpoint/common/climate-common-non-climate.yaml` (line 26), `examples/root-configs/hp-sejour.yaml` (line 80), `examples/root-configs/esp32-test.yaml` (line 82), `examples/root-configs/hp-debug.yaml` (line 49), `examples/xiao-esp32s3/common/wifi.yaml` (line 17), `examples/modular-hvac/packages/hvac-base.yaml` (lines 13-18)
- Current mitigation: `.gitignore` lists `secrets.yaml`. Most example files use `!secret` or `${...}` substitutions. The CI job generates random values into `secrets.yaml` (`.github/workflows/build.yaml`).
- Recommendations: Add a secret-scanning step (for example `gitleaks`) to CI. Keep the README (`examples/dual-setpoint/README.md`, lines 311-335) for guidance only. Those lines show example values that look like real keys; mark them clearly as placeholders.

**Hard-coded CI credentials:**
- Risk: Low. The CI job sets `USERNAME: test` and `PASSWORD: github-run` as literal environment values (`.github/workflows/build.yaml`).
- Files: `.github/workflows/build.yaml` (env block of the `esphome-build` job)
- Current mitigation: The values only exist in a throwaway container.
- Recommendations: Remove them if the examples do not need them, or move them to GitHub secrets.

**Unauthenticated, unencrypted UART link:**
- Risk: The CN105 bus is a physical serial link with no authentication. Anyone with access to the connector can send commands to the heat pump.
- Files: `components/cn105/hp_writings.cpp`, `components/cn105/cn105.cpp`
- Current mitigation: Physical access only. The Home Assistant side is protected by the ESPHome API encryption key when configured.
- Recommendations: Document this in the README, and recommend `api: encryption:` and an OTA password in every example.

## Performance Bottlenecks

**Blocking UART reconnects and fixed delays:**
- Problem: `sendFirstConnectionPacket()` calls `CUSTOM_DELAY(750)` when the UART is not ready (`components/cn105/hp_writings.cpp`, else branch). `reconnectUART()` runs `disconnectUART`, the low-level reset, and `setupUART` synchronously.
- Files: `components/cn105/hp_writings.cpp`, `components/cn105/cn105.cpp:378-384`
- Cause: The reset runs in the main loop, so every reconnect stalls other component work.
- Improvement path: Move reconnect into a state-machine step that is split across loop iterations.

**Excessive `set_timeout` retries on ESP8266:**
- Problem: `emulateMutex` retries every 100 ms and can run up to 10 times before forcing a lock (`components/cn105/utils.cpp:385-416`).
- Files: `components/cn105/utils.cpp`
- Cause: Mutex emulation (see Tech Debt).
- Improvement path: Remove the emulation (see Tech Debt).

## Fragile Areas

**Timing windows and grace periods:**
- Files: `components/cn105/heatpump_profile.cpp:449-485` (`wantedSettings` grace checks), `components/cn105/hp_writings.cpp:257` (300 ms send throttle), `components/cn105/cn105_types.h:8` (`MAX_DELAY_RESPONSE_FACTOR 10`), `components/cn105/cn105_types.h:32` (`DEFER_SCHEDULE_UPDATE_LOOP_DELAY 750`)
- Why fragile: Several independent timers interact: the send throttle, the grace windows, the defer delay, the connection timeout, and the 10-second first-connection check (`hp_writings.cpp:40-60`). Changing one shifts behaviour in the others.
- Safe modification: Change one timer at a time, and add a unit test in `tests/unit/test_setpoint_persistence.cpp` or `tests/unit/test_mode_preservation.cpp` before changing the value.
- Test coverage: Timers are not injectable (see BUG-01 and DEBT above), so tests cannot advance time. Grace-window tests depend on real `millis()` values.

**Installer handshake fallback:**
- Files: `components/cn105/hp_writings.cpp:40-60`, `components/cn105/cn105.h` (`installer_mode_effective_`, `installer_mode_fallback_done_`)
- Why fragile: The code switches from handshake `0x5B` to `0x5A` once, based on a single 10-second timeout. A slow heat pump can trigger the fallback incorrectly. The `installer_mode_fallback_done_` flag is set once and the effect on later reconnects was not checked.
- Safe modification: Test both handshakes against a unit that is known to respond slowly before changing the timeout.
- Test coverage: `tests/unit/test_protocol.cpp` covers packet bytes, not the timeout path.

**Packet length and checksum handling:**
- Files: `components/cn105/hp_writings.cpp:96-120`, `components/cn105/cn105_protocol.h`, `components/cn105/frame_parser.h`
- Why fragile: `writePacket` does its length check only in the not-ready branch, after `reconnectUART()` has already run. The `CONNECT` checksum is pre-computed and recomputed manually (`hp_writings.cpp:~20`).
- Safe modification: Route all packet creation through `cn105_protocol` helpers and cover them in `tests/unit/test_checksum.cpp`.

**Shared mutable `wantedSettings`:**
- Files: `components/cn105/climateControls.cpp:15-70`, `components/cn105/extraComponents.cpp` (writes to `wantedSettings.lastChange` in 6 places), `components/cn105/heatpump_profile.cpp` (grace logic)
- Why fragile: Many entry points (climate control, extra components, profile decode) mutate the same struct and rely on `hasChanged`, `hasBeenSent` and `lastChange` in a particular order.
- Safe modification: Change `wantedSettings` only through its setter helpers, and keep `resetSettings()` as the only place that clears it.

## Scaling Limits

**Number of heat pumps per ESP:**
- Current capacity: One `CN105Climate` per UART. `climate.py` clamps the port index to 0..2 (`components/cn105/climate.py`, `get_uart_port_index`).
- Limit: Three UART ports on most ESP32 boards. Each instance has its own timers and mutexes.
- Scaling path: Multi-split setups need a second CN105 device on a second UART. Mode conflicts between indoor units on the same outdoor unit are not detected (FEAT-07 in the audit).

**Energy counter range:**
- Current capacity: 6553.5 kWh (16-bit value, divided by 10).
- Limit: Reached after several years on high-consumption units.
- Scaling path: See BUG-09 above.

## Dependencies at Risk

**ESPHome internal API:**
- Risk: The component uses ESPHome internals through `components/cn105/esphome_compat.h` and `components/cn105/componentEntries.cpp`. Each ESPHome release can change these (the MAC address handling change in commit `3d0adee` is one example).
- Impact: Build failures on `stable` or `beta` after an ESPHome update.
- Migration plan: Keep all version-specific calls behind `esphome_compat.h`. Add the `beta` CI job (see Test Coverage Gaps).

**Home Assistant integration API:**
- Risk: `custom_components/mitsubishi_climate_proxy/climate.py` (1,080 lines) calls many `hass.services.async_call` endpoints (lines 714-849) for climate, number and switch entities. Service names and data keys are not checked against a Home Assistant version.
- Impact: Silent failures when HA renames a service or field.
- Migration plan: Pin a minimum HA version in `custom_components/mitsubishi_climate_proxy/manifest.json` and add tests for the service-call payloads.

## Missing Critical Features

**No unavailable state in Home Assistant:**
- Problem: When the heat pump stops responding, HA keeps the last values (`components/cn105/componentEntries.cpp`, `components/cn105/cn105.cpp:405`).
- Blocks: Users cannot tell "heat pump is off" from "no data". Automations can act on stale data.
- Audit reference: FEAT-01. Use `status_set_warning()` or a connectivity `binary_sensor`.

**No command confirmation or retry:**
- Problem: Commands are sent once and treated as applied (see BUG-03).
- Blocks: Reliable automations that depend on a command taking effect.
- Audit reference: FEAT-04.

**No link diagnostics:**
- Problem: Checksum errors, expired cycles, reconnects and response latency are not exposed as sensors.
- Blocks: Diagnosing intermittent wiring or power problems from Home Assistant.
- Audit reference: FEAT-05.

## Test Coverage Gaps

**Time-dependent logic:**
- What's not tested: Behaviour around the `millis()` wrap, `deferCycle`, and `doesCycleTimeOut` (`components/cn105/cycle_management.cpp`).
- Files: `components/cn105/cycle_management.cpp`, `tests/unit/` (no cycle test file exists)
- Risk: BUG-01 is not caught by the current 287 unit tests (count reported in the audit; not re-run for this document).
- Priority: High

**Remote temperature decision:**
- What's not tested: The margin comparison in `hp_readings.cpp:84` with a real-valued difference.
- Files: `components/cn105/hp_readings.cpp`, `tests/unit/test_temperature.cpp`
- Risk: BUG-02 passes all current tests because the test values are whole degrees.
- Priority: High

**Write path and reconnect:**
- What's not tested: Queued writes, `pending_packet_` replacement, and reconnect throttling in `writePacket`.
- Files: `components/cn105/hp_writings.cpp`, `tests/unit/test_mode_preservation.cpp`
- Risk: BUG-03 and BUG-04 can regress without warning.
- Priority: Medium

**UART port selection:**
- What's not tested: `get_uart_port_index` in `components/cn105/climate.py`, and `force_low_level_uart_reinit` in `components/cn105/cn105.cpp`. Both need hardware or ESP-IDF.
- Files: `components/cn105/climate.py`, `components/cn105/cn105.cpp`
- Risk: BUG-05 may reconfigure the logger UART.
- Priority: Medium

**Home Assistant proxy integration:**
- What's not tested: `custom_components/mitsubishi_climate_proxy/climate.py` and `config_flow.py` have no automated tests in the repository.
- Files: `custom_components/mitsubishi_climate_proxy/climate.py`, `custom_components/mitsubishi_climate_proxy/config_flow.py`
- Risk: Service payload or temperature conversion regressions reach users unnoticed.
- Priority: Medium

---

*Concerns audit: 2026-10-27*
