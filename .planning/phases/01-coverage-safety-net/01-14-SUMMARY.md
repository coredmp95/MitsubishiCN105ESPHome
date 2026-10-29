---
phase: 01-coverage-safety-net
plan: 14
subsystem: testing
tags: [googletest, characterization, entities, sensors, selects, switches, hardware-settings, lossnay, esp32, esp8266]

requires:
  - phase: 01-coverage-safety-net
    provides: "01-01..01-03 fixture API (Cn105Fixture, run_poll_cycle, take_tx_frames, TestLog), real_frames.h, ESP32 + ESP8266 -m32 harness variants; 01-10 Lossnay connect helper pattern; 01-12 decision existing-only and Wanted captures list"
provides:
  - "Entities suites (16 tests): every diagnostic sensor and text sensor, vertical/horizontal vane and airflow selects, HVAC option switches and the uptime connection sensor pinned through their production setters"
  - "HardwareSettings suites (14 tests): installer-mode connection, 0x20/0x22 scheduling, functions get/set buttons, code/value numbers, functions text sensor and HardwareSettingSelect behaviour"
  - "LossnaySpecifics suite (6 tests): Lossnay request list, actual mode answered to its own 0x09 request, allowed sensors, refused run-state and functions commands"
  - "Header-only entity classes (kwh, input power, runtime, outside air, humidity, iSee, stage, sub-mode, auto sub-mode, error code, vane select, HVAC switch, functions button/number/sensor, uptime) are now instantiated by the harness"
affects: [01-15, phase-06-diagnostic-sensors, phase-08-encapsulation]

actuals:
  tokens: 17100
  tasks: 3
  commits: 4

tech-stack:
  added: []
  patterns:
    - "One fixture subclass per entity configuration; before_setup() constructs the entity, names it with configure_entity_() and calls the production setter, as climate.py to_code does"
    - "Publishes observed with add_on_state_callback into small Recorder / SelectRecorder / SwitchRecorder helpers"
    - "Functions state seeded directly with bytes laid out from the documented encoding when no 0x20/0x22 capture exists: test data for the encoder, never a frame fed to the parser"
    - "Heat-pump real captures used as labelled payload carriers in Lossnay poll cycles, never presented as Lossnay captures"

key-files:
  created:
    - tests/unit/characterization/test_entities.cpp
    - tests/unit/characterization/test_hardware_settings.cpp
    - tests/unit/characterization/test_lossnay_specifics.cpp
    - .planning/phases/01-coverage-safety-net/deferred-items.md
  modified: []

key-decisions:
  - "Two 0x06 status frames quoted in the decode_status() comment of heatpump_profile.cpp (26 Hz compressor; 8 W / 136.0 kWh) are used as the only non-zero status captures; the test proves their quoted checksums 0x3C and 0xB7 match, which is what distinguishes them from hand-made frames. Kept local to test_entities.cpp (real_frames.h is not in this plan's file list)"
  - "No 0x42, 0x20 or 0x22 reply and no Lossnay handshake or 0x03/0x06/0x04 reply is fed: no capture exists (plan 01-12 existing-only). Those decode paths stay unpinned and remain under Wanted captures; no new README row was needed (no genuinely new scenario)"
  - "Installer-mode suites connect by feeding the real standard 0x7A reply to the 0x5B CONNECT (the profile accepts 0x7A and 0x7B); a real 0x7B reply remains a wanted capture"
  - "Lossnay poll cycles answer 0x03/0x06/0x04 with real heat-pump captures as payload carriers, labelled in the file header as not Lossnay captures"

patterns-established:
  - "Unanswered requests: those with a soft timeout (0x09, 0x42, 500 ms) let the cycle move on, those without (0x03, 0x20, 0x22) are ended by the 3 s cycle timeout and the rest waits for the next cycle"

requirements-completed: [COV-04]

coverage:
  - id: D1
    description: "Diagnostic sensors and text sensors (compressor frequency, input power, kWh, runtime hours, outside air temperature, target humidity, iSee, stage, sub-mode, auto sub-mode, error code) pinned from a real poll cycle: values and publish counts over four cycles, NaN first publish, report_when_idle warning"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "EntitiesSensors.Characterize_PollCyclePublishesDiagnostics, Characterize_ReportWhenIdleWarningLoggedOnce, EntitiesSensorsReportWhenIdle.Characterize_ReportWhenIdleOptionSilencesWarning (tests/unit/characterization/test_entities.cpp)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Vertical and horizontal vane selects, airflow control select and the three HVAC option switches pinned from user action to the 0x41 bytes written (vertical vane equals the real SET_VaneUp and SET_VaneSwing captures) and to the published state"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "EntitiesVerticalVane.*, EntitiesHorizontalVane.*, EntitiesHorizontalVaneOptions.*, EntitiesAirflow.*, EntitiesHvacOptions.* (tests/unit/characterization/test_entities.cpp)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Uptime connection sensor pinned through the bootstrap (three 0 publishes), a polling tick and a connection loss"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "EntitiesUptime.Characterize_UptimeConnectionSensorPublishes; LossnaySpecifics.Characterize_LossnayAllowedSensorsPublish"
        status: pass
    human_judgment: false
  - id: D4
    description: "Installer functions pinned: 0x20/0x22 request order and interval, get/set buttons, code/value numbers (out of range refused, not clamped), the two function packets 0x1F/0x21, functions text sensor and HardwareSettingSelect guards. Reply decoding is not pinned (no capture)"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "HardwareSettings.*, HardwareSettingsShortInterval.*, HardwareSettingsNoSelects.* (tests/unit/characterization/test_hardware_settings.cpp)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Lossnay specifics pinned: request list and poll order, actual mode HEATING/FAN from the captured 0x09 frames, OFF transition, allowed sensors, run-state and functions commands refused with the unsupported-operation warning"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "LossnaySpecifics.* (tests/unit/characterization/test_lossnay_specifics.cpp)"
        status: pass
    human_judgment: false
  - id: D6
    description: "components/cn105 unchanged (D-03) and no invented frame presented as a capture"
    requirement: COV-04
    verification:
      - kind: other
        ref: "git diff --quiet origin/main -- components/cn105; ! grep -n SYNTHETIC tests/unit/characterization/test_lossnay_specifics.cpp"
        status: pass
    human_judgment: false

duration: ~15min
completed: 2026-10-30
status: complete
plan_head_before: cb4d8ca9a58cde6937c52b377a9bae1dfcc95ec4
plan_head_after: 7550c639397ef28b6bba73a347c26cd2383fca99
commits: 4
---

# Phase 1 Plan 14: Entities, installer functions and Lossnay specifics Summary

**The remaining surface of the component (diagnostic sensors, vane/airflow selects, HVAC switches, uptime sensor, installer functions and Lossnay entity restrictions) is pinned through production setters and real ESPHome entity objects on both variants, with no invented capture and `components/cn105` untouched.**

## Performance

- **Duration:** ~15 min (commit clock 00:00 to 00:12 on 2026-10-30; start time was not captured)
- **Tasks:** 3 (1 tracer, 2 auto)
- **Files:** 3 test files created, 1 deferred-items file created

## Accomplishments

- 36 new tests (16 `Entities*`, 14 `HardwareSettings*`, 6 `LossnaySpecifics`), identical on ESP32 and ESP8266. Full harness: ESP32 168/168, ESP8266 166/166, `[run-harness] OK: esp32 esp8266`.
- Tracer gate (automated-only verify, end-of-phase mode): Task 1 verify passed on both variants (5 tests, 20+ entity publishes asserted) before expanding.
- Coverage measured by `tests/unit/coverage.sh --docker` (informational, baseline file untouched): line 84.87% (2648/3120), branch 73.84% (1578/2137) against the recorded baseline 67.05% / 60.08%. `cycle_management.h`, `hardware_setting_select.h`, `heatpumpFunctions.h`, `request_scheduler.h` and `uptime_connection_sensor.h` are listed in the report's header-only section. Ratcheting the baseline is for plan 01-15.
- `git diff --quiet origin/main -- components/cn105` clean (D-03).

## Task Commits

1. **Task 1 (tracer): diagnostic sensors from a real poll cycle** - `8b86752` (test)
2. **Task 2: controls, uptime sensor and installer functions** - `b6b7eac` (test)
3. **Task 2/3 tidy: uptime bootstrap pin and comments** - `e30ce96` (test)
4. **Task 3: Lossnay specifics** - `7550c63` (test)

**Plan metadata:** the docs commit that carries this SUMMARY.

## Observations pinned (asserted as observed)

- **Status sensors publish twice in the first cycle.** The poll order is 0x02, 0x03, 0x06: the 0x03 reply reaches `statusChanged()` while compressor frequency, input power and energy are still NaN, so those sensors first publish NaN, then the real value after 0x06. Later cycles publish once per changed status; an unchanged 0x06 republishes nothing.
- **iSee** is offered at every settings frame but the binary sensor drops a repeat (1 publish over 4 cycles). **Error code** text is republished at every cycle even when unchanged (text sensors do not dedupe). Stage, sub-mode and auto sub-mode publish only on change.
- **Vane selects publish on read-back only.** A user selection writes the SET frame (vertical: byte for byte the real `SET_VaneUp` / `SET_VaneSwing` captures) and does not touch the select; the select follows the next 0x02 reply. The airflow select, when refused (wide vane not "AIRFLOW CONTROL"), re-publishes the current value so Home Assistant snaps back; when allowed it publishes the wanted value optimistically.
- **HVAC option switches** publish optimistically when the run-states packet is written (`0x41`, byte 5 `0x08`, flag in byte 7, value in byte 17/18/19). With a switch attached the 0x42 request sits between 0x09 and 0x04; unanswered, its 500 ms soft timeout lets the cycle move on.
- **Uptime connection sensor** publishes 0 three times on the way to the connection (two `stop()` calls in the bootstrap, then `start()`), and 0 again on connection loss.
- **0x20 / 0x22 scheduling.** With a hardware setting select attached and no reply, 0x20 leaves in the first cycle, 0x22 in the second (0x20 has no soft timeout, so the 3 s cycle timeout ends the first cycle). Then both stay quiet for the YAML interval (24 h default; 5 s variant tested, gaps at least 5 s). Without selects they are disabled; the get button re-enables them on demand.
- **Number entities refuse, not clamp.** A code of 300 or a value of 0 is dropped with the stored value unchanged.
- **Select before get.** A `HardwareSettingSelect` change when the functions were never read queues `isSetFunctions_` but writes nothing (`encode_functions` refuses invalid functions).
- **Lossnay.** Request list is the heat-pump one; `0x42` canSend refuses, `0x05/0x20/0x22` are disabled. Run-state packets and functions commands are dropped with `Ignoring unsupported Lossnay operation (3)` and the wanted state is reset.

## Decisions Made

See key-decisions above. Nothing beyond what the plan prescribed: capture-dependent reply decoding stays unpinned.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Test design] Expectations corrected after the first run**
- **Found during:** Tasks 1-3
- **Issue:** first drafts expected one publish per status sensor in cycle 1 (actually two, NaN first), a binary-sensor publish per settings frame (deduped), an unanswered 0x42 ending the cycle (the soft timeout moves on to 0x04), clamping of out-of-range numbers (refused), and the left arrow as wide-vane byte 0x01 (it is 0x02).
- **Fix:** assertions now pin the observed behaviour with the mechanism in a comment. No production change.
- **Files modified:** the three test files
- **Commits:** `8b86752`, `b6b7eac`, `7550c63`

### Plan-reading adjustments

- Entities are named with `configure_entity_()` and observed with callbacks, but are **not registered with `esphome::App`**: nothing in the component reads the registry, and the 01-12 tests do the same. Registration would only add `App` entity-count coupling.
- `Characterize_HvacOptionsReplyDecoded`, `Characterize_FunctionsRepliesDecoded` and `Characterize_LossnayCapturedHandshakeReplyConnects` were **not written**: 01-12's SUMMARY records no supplied capture for 0x42, 0x20/0x22 or the Lossnay handshake. No new Wanted-captures row was needed (all three scenarios are already listed).
- Wide-vane SET, airflow SET and HVAC run-state SET bytes have no capture: expected bytes are laid out from the documented packet format and the tests say so. Functions state is seeded directly (`hp().functions.setData1/2`) for the set-button, select and `functionsArrived()` tests.
- Installer-mode suites connect with the real standard 0x7A reply (accepted by the profile) because the 0x7B reply is a wanted capture.

**Total deviations:** 1 test-design auto-fix (Rule 1) and 4 plan-reading adjustments, none touching `components/cn105`.

## Issues Encountered

- **Pre-existing, out of scope:** `RemoteTemperatureEntities.Characterize_SameValueDebounced` (plan 01-12) fails on the ESP32 variant when run alone or under `ctest` (each test in its own process): `remote_temp_debounce_skip_count_` is 2, not 1. It passes in the full one-process harness run. Probable cause: the ESP32 platform stub `random_bytes()` is a process-global sequence, so ESPHome's random interval offset for the keep-alive depends on test order. Effect: `coverage.sh` reports "tests failed (ctest exit 8)" and the CI ctest step would fail the same way. Logged in `.planning/phases/01-coverage-safety-net/deferred-items.md`; not fixed here (files outside this plan).

## Verification Results

- `tests/unit/run-harness.sh --docker --variant all --filter 'Entities*.*:HardwareSettings*.*'`: 30 tests per variant, OK.
- `tests/unit/run-harness.sh --docker --variant all --filter 'LossnaySpecifics.*'` (with the others): 36 tests per variant, OK.
- Full harness: ESP32 168, ESP8266 166, all pass.
- `grep -c 'set_installer_mode\|installer_mode = true' test_hardware_settings.cpp` = 1; `grep -c 'LOSSNAY_ACTUAL_' test_lossnay_specifics.cpp` = 11; no `SYNTHETIC` in `test_lossnay_specifics.cpp`.
- `git diff --quiet origin/main -- components/cn105`: exit 0.

## User Setup Required

None.

## Known Stubs

None.

## Threat Flags

None. T-01-28: no 0x42/0x20/0x22/Lossnay-handshake reply is fed. T-01-29: `components/cn105` unchanged.

## Next Phase Readiness

- Ready for 01-15 (coverage baseline ratchet and CI wiring). Before the ratchet is committed, the order-dependent 01-12 test (deferred-items.md) should be fixed, otherwise `coverage.sh` and the CI ctest step report a failure.
- Phase 6 (diagnostic sensors, CMD-04) can flip the first-publish NaN pins and the optimistic switch / select pins; Phase 8 (encapsulation) is covered by the entity setters now exercised.

## Self-Check: PASSED

- FOUND: tests/unit/characterization/test_entities.cpp
- FOUND: tests/unit/characterization/test_hardware_settings.cpp
- FOUND: tests/unit/characterization/test_lossnay_specifics.cpp
- FOUND: commits 8b86752, b6b7eac, e30ce96, 7550c63
