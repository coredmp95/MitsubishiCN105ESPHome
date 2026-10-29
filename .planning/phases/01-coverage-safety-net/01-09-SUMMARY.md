---
phase: 01-coverage-safety-net
plan: 09
subsystem: testing
tags: [googletest, characterization, climate-controls, bug-03, bug-08, m32, esp32, esp8266, emulate-mutex]

requires:
  - phase: 01-coverage-safety-net
    provides: "01-01..01-03 fixture API (Cn105Fixture, publishes_, take_tx_frames, run_poll_cycle), real_frames.h, TestLog/TestPrefs, ESP32 + ESP8266 -m32 harness variants, run-harness.sh"
provides:
  - "ClimateControls suites (6 fixture subclasses): 14 tests on ESP32, 16 on ESP8266"
  - "End-to-end pin of a ClimateCall: control() -> wantedSettings -> immediate publish -> SET frame -> second publish -> reset"
  - "Mode / fan / swing / temperature mappings pinned with exact 0x41 frame bytes"
  - "Debounce boundary, hasBeenSent short-circuit and 300 ms strict throttle pinned"
  - "Both platform locking paths of control() pinned (std::lock_guard vs emulateMutex incl. the retry schedule)"
affects: [01-12, phase-04-time, phase-06-command-reliability, phase-07-cleanup]

actuals:
  tokens: 7765
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Option subclasses of the suite (ClimateControlsHeatCool, ...ExtraFans, ...Debounce, ...ClockNear2Pow31, ...RestoreSetpoints) adjust traits / setters / start clock through before_setup() and options()"
    - "Platform-specific tests live in one #if defined(USE_ESP32) / #else block; test names contain Esp32 / Esp8266 so the plan's filters select them"
    - "set_frame({{index, value}, ...}) builds the expected 0x41 frame from a zeroed template, so each assertion shows only the bytes that matter"

key-files:
  created:
    - tests/unit/characterization/test_climate_controls.cpp
  modified: []

key-decisions:
  - "Frames are asserted whole (22 bytes with checksum) rather than byte by byte, so any drift in flags or encoding is caught"
  - "Mapping loops re-run one poll cycle between commands so every command starts from the same confirmed state"
  - "The 1 ms stepping is used wherever a timing is pinned to the millisecond (emulateMutex retry schedule)"

patterns-established:
  - "Bug pins named Characterize_BUG<NN>_... with '// Pins current buggy behaviour — flip in Phase N (REQ-ID)' directly above"

requirements-completed: [COV-04]

coverage:
  - id: D1
    description: "A command is pinned end to end: HA publish at control() time (BUG-03), wantedSettings fields, no byte on the wire, SET frame at lastSend+310 ms, second publish at send time, wantedSettings reset (lastChange left behind)"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ClimateControls.Characterize_BUG03_OptimisticPublishBeforeSend (test_climate_controls.cpp)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Mode (OFF/HEAT/DRY/COOL/FAN_ONLY/AUTO, HEAT_COOL -> hardware AUTO), fan (AUTO/LOW/MEDIUM/HIGH + QUIET/MIDDLE/DIFFUSE/ON and FAN_OFF -> power OFF), swing (OFF/VERTICAL) and temperature-only mappings with exact SET frame bytes"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ClimateControls.Characterize_ModeMapping, ClimateControlsHeatCool.Characterize_ModeMappingHeatCoolIsHardwareAuto, ClimateControls.Characterize_FanMapping, ClimateControlsExtraFans.Characterize_FanMappingExtraModes, ClimateControls.Characterize_SwingMapping, ClimateControls.Characterize_TemperatureChangeOnly"
        status: pass
    human_judgment: false
  - id: D3
    description: "Send gating: debounce boundary (first send exactly lastChange + debounce_delay), hasBeenSent short-circuit, 300 ms write throttle (strict >)"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ClimateControlsDebounce.Characterize_DebounceBoundary, ClimateControls.Characterize_HasBeenSentShortCircuits, ClimateControls.Characterize_ThrottleWithin300msOfLastSend"
        status: pass
    human_judgment: false
  - id: D4
    description: "BUG-08: with millis() crossing 2^31 under -m32 the debounce arithmetic still sends at lastChange + debounce_delay (it relies on signed wrap-around)"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ClimateControlsClockNear2Pow31.Characterize_BUG08_DebounceSignedNowPast2Pow31"
        status: pass
    human_judgment: false
  - id: D5
    description: "Platform locking: ESP32 applies synchronously under std::lock_guard; ESP8266 emulateMutex applies synchronously when free, defers while locked (first retry after 100 ms) and forces the unlock after 10 retries (deferrals at 0,100,220,364,536,742,989,1285,1640,2066 ms, forced at 2577 ms, flag cleared)"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ClimateControls.Characterize_Esp32ControlAppliesImmediatelyUnderLockGuard; ClimateControls.Characterize_Esp8266EmulateMutexAppliesImmediatelyWhenFree, ..._DefersWhileLocked, ..._ForcesUnlockAfter10Retries"
        status: pass
    human_judgment: false
  - id: D6
    description: "Setpoint persistence: the record is written at control() time (not at send time), only with restore_setpoints enabled"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ClimateControlsRestoreSetpoints.Characterize_SetpointSavedToPreferences, ClimateControls.Characterize_SetpointNotSavedWithoutRestoreOption"
        status: pass
    human_judgment: false
  - id: D7
    description: "components/cn105 unchanged"
    requirement: COV-04
    verification:
      - kind: other
        ref: "git diff --quiet origin/main -- components/cn105 (after every task)"
        status: pass
    human_judgment: false

duration: ~35min
completed: 2026-10-29
status: complete
plan_head_before: 510041bc80834a2dc0f87c76113f2e851e867dd9
plan_head_after: 74142849d30878df77114a4d0ca5aad35a28ceae
commits: 3
---

# Phase 1 Plan 09: climateControls characterization Summary

**The command path of the unmodified component is pinned on both 32-bit variants: a ClimateCall travels control() -> wantedSettings -> optimistic HA publish (BUG-03) -> 0x41 SET frame -> second publish -> reset, with exact frame bytes for every mode/fan/swing mapping, the debounce/throttle gates, the BUG-08 clock-crossing case and both platform locking strategies (std::lock_guard / emulateMutex) that Phase 7 removes.**

## Performance

- **Duration:** ~35 min (start time not captured; estimated)
- **Completed:** 2026-10-29
- **Tasks:** 3 (1 tracer + 2 auto)
- **Files:** 1 created (test file only)

## Accomplishments

- 17 `TEST_F` in the file; selected by `ClimateControls*`: 14 on ESP32, 16 on ESP8266 (the three emulateMutex tests replace the single lock_guard one). Full harness run: ESP32 84/84, ESP8266 85/85.
- Tracer gate (automated-only verify, end-of-phase mode): Task 1 verify passed on both variants (`[run-harness] OK`) before expanding.
- Assertions were mutation-checked once (changing a retry time makes `ForcesUnlockAfter10Retries` fail), so the passing runs are not vacuous.

## Task Commits

1. **Task 1 (tracer): one command end to end, BUG-03 pin** - `35038c1` (test)
2. **Task 2: mappings, debounce, throttle, BUG-08** - `627b55c` (test)
3. **Task 3: platform locking paths and setpoint persistence** - `7414284` (test)

## Observed values worth knowing (pinned)

- A `make_call().set_mode(HEAT).set_target_temperature(23).perform()` publishes exactly once at control() time (same millisecond), with nothing written. The SET frame is `FC 41 01 30 10 01 07 00 01 01 00 00 00 00 00 00 00 00 00 AE 00 C6`: temperature goes in byte 19 as `T*2+128` (encoding B, byte 10 stays 0) with flag 0x04. It leaves 310 ms after the previous write (poll request 10 ms before the command, strict `> 300` throttle, 10 ms ticks). A second, identical publish happens in the same call that writes the frame. `wantedSettings` is then reset but `lastChange` keeps its value.
- Flags in byte 6: power 0x01, mode 0x02, temperature 0x04, fan 0x08, vane 0x10. OFF sets the power flag only; every other mode sets power ON. FAN_ONLY -> "FAN" (0x07), AUTO 0x08, COOL 0x03, DRY 0x02, HEAT 0x01. HEAT_COOL is sent exactly as AUTO while the entity stays in HEAT_COOL.
- Fan: LOW "1" 0x02, MEDIUM "2" 0x03, HIGH "4" 0x06, MIDDLE "3" 0x05, QUIET and DIFFUSE both "QUIET" 0x01, ON and AUTO "AUTO" 0x00. `CLIMATE_FAN_OFF` is not a fan speed: it turns the whole unit OFF (power flag only).
- Swing OFF only acts when the confirmed (polled) vane is SWING (it sends AUTO); swing VERTICAL sends SWING (0x07). A test starting from a real swinging state (`SETTINGS_FAN2_VANE_SWING`) was used for this.
- Debounce: with `debounce_delay: 500` the frame leaves exactly 500 ms after the command (strict `<`). With the default (field zero-filled by the fixture) the throttle is the only gate.
- emulateMutex (ESP8266): free -> synchronous. Held -> one deferral log per attempt; the first retry fires 100 ms after the call, later delays are multiplied by 1.2 (float, truncated): attempts at 0, 100, 220, 364, 536, 742, 989, 1285, 1640, 2066 ms, forced run at 2066+511 = 2577 ms with the "10 retry calls failed" warning. The forced run clears `wantedSettingsMutex` although its foreign holder never released it.
- Setpoint record: written inside `controlDelegate()` at control() time, not at send time, and only with `restore_setpoints`; the preference slot is empty after setup.

## Deviations from Plan

### Plan reading contradicted by observation (characterization rule: assert observed, comment, report)

**1. [Observation] BUG-08 does not misbehave at the 2^31 crossing**
- **Found during:** Task 2
- **Issue:** The plan expected `Characterize_BUG08_DebounceSignedNowPast2Pow31` to pin a visible quirk once `long now` turns negative. With `-m32` the command issued ~100 ms before 2^31 is sent exactly 500 ms later, same as far from the boundary: the `long` subtraction wraps (signed overflow, undefined behaviour in C++), and the result is compared as unsigned (`uint32_t` debounce), and `lastSend` is `unsigned long`.
- **Fix:** The test pins this observed result (sent at lastChange + debounce_delay, `long now` negative at that time) and its comment says the correctness rests on UB wrap-around. The Phase 4 (TIME-03) flip is therefore "same behaviour without UB", not a visible behaviour change; the audit's other BUG-08 half (uninitialised fields, here `debounce_delay_` and `lastSend`) is not covered by this plan: the fixture zero-fills the object on purpose (the debounce test sets the delay explicitly).

**2. [Observation] ESP8266 has three mutex tests, ESP32 one**
- **Found during:** Task 3
- **Issue:** The verify filters require at least one `Esp8266` and one `Esp32` test, as designed; an additional `Characterize_Esp8266EmulateMutexAppliesImmediatelyWhenFree` was added for the uncontended path (plan says "without contention" in Task 1 only through the tracer).
- **Fix:** Added; no production change.

**Total deviations:** 2 observations, 0 production changes. **Impact:** none on scope.

## Real captures needed

- None for this plan: all response frames are real (`SETTINGS_ON_HEAT_21_5`, `SETTINGS_FAN2_VANE_SWING`), and the SET frames are asserted against the encoder's layout. The six real SET captures were already compared in plan 01-08. Two scenarios would still help a later plan: a real 0x41 SET carrying a `wideVane` change on a unit with horizontal vanes (swing HORIZONTAL/BOTH are not advertised by the fixture traits, so they are not pinned here), and a Fahrenheit-mode command capture.

## Issues Encountered

An early attempt to create the test file with `mkdir ... && python` failed silently (the `mkdir` of an existing directory short-circuited the chain); caught because the harness selected 0 tests, and redone with the Write tool.

## Verification Results

- `tests/unit/run-harness.sh --docker --variant all --filter 'ClimateControls*'`: 14 tests (ESP32) / 16 tests (ESP8266), `[run-harness] OK: esp32 esp8266`.
- `--variant esp8266 --filter 'ClimateControls*Esp8266*'`: 3 tests, OK; `--variant esp32 --filter 'ClimateControls*Esp32*'`: 1 test, OK.
- `tests/unit/run-harness.sh --docker --variant all`: ESP32 84/84, ESP8266 85/85.
- Acceptance greps: `flip in Phase 6 (CMD-03)` x1, `Characterize_BUG08_` x1 with `flip in Phase 4 (TIME-03)`, `CLEAN-01` x3, `defined(USE_ESP32)` x1; the mode mapping test asserts HEAT_COOL -> wanted mode "AUTO".
- `git diff --quiet origin/main -- components/cn105`: clean after every task (D-03).

## User Setup Required

None.

## Known Stubs

None.

## Threat Flags

None. T-01-17 (BUG-03 pin named and commented with its fixing phase) and T-01-18 (no production edit) mitigated as verified.

## Next Phase Readiness

- Phase 6 flips `Characterize_BUG03_OptimisticPublishBeforeSend` (CMD-03); Phase 4 revisits `Characterize_BUG08_*` (TIME-03); Phase 7 replaces the three emulateMutex tests and the lock_guard test when CLEAN-01 removes the mutex. The restore side of setpoint persistence belongs to plan 01-10.

## Self-Check: PASSED

- File exists: `tests/unit/characterization/test_climate_controls.cpp`.
- Commits `35038c1`, `627b55c`, `7414284` are ancestors of HEAD.
