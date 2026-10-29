---
phase: 01-coverage-safety-net
plan: 10
subsystem: testing
tags: [googletest, characterization, heatpump-profile, lossnay-profile, setpoint-grace, dual-setpoint, m32, esp32, esp8266]

requires:
  - phase: 01-coverage-safety-net
    provides: "01-01..01-03 fixture API (Cn105Fixture, publishes_, take_tx_frames, run_poll_cycle), real_frames.h, TestLog/TestPrefs/TestClock, ESP32 + ESP8266 -m32 harness variants, run-harness.sh"
provides:
  - "ProfileState (+ Stage, DualSetpoint, RestoreSetpoints subclasses) and ProfileStateLossnay suites: 17 tests per variant, identical on ESP32 and ESP8266"
  - "A real 0x02 frame pinned from bytes to currentSettings and the published HA state"
  - "The user-demand guards of HeatPumpProfile pinned with their observable effects, including two dead branches"
  - "Lossnay mode pinned through the real component: CONNECT with profile 0x34, refused operations, captured 0x02/0x09 decoding, control frames vs test_lossnay.cpp checksums"
affects: [01-12, phase-06-command-reliability, phase-08-architecture]

actuals:
  tokens: 10392
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "poll_with_hook(responses, code, hook): runs a user command between a request and its answer, the only way a received 0x02 can meet a pending command (loop() starts no poll cycle while a command waits)"
    - "Dead or unreachable production branches are pinned by their absence (EXPECT_FALSE on the log) next to a positive check that the log level is captured, plus a forced-state test of the helper itself"
    - "Lossnay suite reaches the connected state through the production handleConnectionSuccess() after a real (rejected) heat-pump reply stamps lastResponseMs; no Lossnay handshake frame is invented"

key-files:
  created:
    - tests/unit/characterization/test_profile_state.cpp
  modified: []

key-decisions:
  - "Dead log lines are asserted absent, not removed from the plan: 'Ignoring setpoint: pending user temp' and 'post-send grace' cannot be written by the production flow (see Observations); the pins flip when Phase 6 reworks readback"
  - "Lossnay connected state = real CONNECT_REPLY fed (rejected, stamps lastResponseMs) + hp().handleConnectionSuccess(); setHeatpumpConnected(true) alone would not have helped because the failing piece was lastResponseMs, not the connected flag"
  - "Real Lossnay 0x09 captures are fed as the answer to the 0x03 request (the scheduler dispatches by response code), because no Lossnay 0x03/0x06 capture exists to let the cycle reach 0x09 naturally"

patterns-established:
  - "Option subclasses (ProfileStateStage, ProfileStateDualSetpoint, ProfileStateRestoreSetpoints) attach sensors / traits / setters in before_setup()"

requirements-completed: [COV-04]

coverage:
  - id: D1
    description: "A real received 0x02 settings frame is pinned from the bytes to currentSettings and the published climate state (ON/HEAT/21.5 then OFF/HEAT/19.5)"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ProfileState.Characterize_DecodeRealSettingsUpdatesCurrentAndPublishes (tests/unit/characterization/test_profile_state.cpp)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Guards protecting a user demand: incoming setpoint ignored while a user temperature is pending (silently), post-send grace helper (unreachable through the real flow), 3000 ms received-setpoint grace with its log line and boundary, 3000 ms vane grace"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ProfileState.Characterize_IncomingSetpointIgnoredWhilePendingUserTemp, Characterize_IncomingSetpointIgnoredWithinPostSendGrace, Characterize_IncomingSetpointDisagreesWithLastUser, Characterize_IncomingVaneIgnoredWithinGrace"
        status: pass
    human_judgment: false
  - id: D3
    description: "Action / stage / sub-mode derived from real 0x06 and 0x09 frames, with and without the stage-as-operating fallback"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ProfileStateStage.Characterize_UpdateActionFromStatusAndPower, ProfileState.Characterize_ActionIgnoresStageWithoutFallback"
        status: pass
    human_judgment: false
  - id: D4
    description: "Dual setpoint: HEAT_COOL band sent as hardware AUTO with ONE setpoint (deadband on the room temperature), low/high handling with the 600 ms UI anti-rebound, save/restore of the band through preferences"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ProfileStateDualSetpoint.Characterize_HeatCoolDualSetpoint, ProfileStateRestoreSetpoints.Characterize_SetpointsSavedAndRestored"
        status: pass
    human_judgment: false
  - id: D5
    description: "Lossnay: CONNECT frame with profile 0x34, rejected heat-pump handshake reply, refused operations and warnings, ignored temperature change, captured 0x02/0x09 decoding, control frames compared with test_lossnay.cpp checksums"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ProfileStateLossnay.* (8 tests)"
        status: pass
    human_judgment: false
  - id: D6
    description: "components/cn105 unchanged and no synthetic frame used"
    requirement: COV-04
    verification:
      - kind: other
        ref: "git diff --quiet origin/main -- components/cn105; ! grep -n SYNTHETIC tests/unit/characterization/test_profile_state.cpp"
        status: pass
    human_judgment: false

duration: ~45min
completed: 2026-10-29
status: complete
plan_head_before: dfffcb410fab57d1c5c06fb08899e3a729946d3b
plan_head_after: bc7a30089bb9b0d2f56927bbc1b4c87ff5e2e1de
commits: 3
---

# Phase 1 Plan 10: Profile state handling characterization Summary

**HeatPumpProfile's user-demand guards, action/stage derivation, HEAT_COOL dual setpoint with persistence, and the whole LossnayProfile (profile 0x34 CONNECT, refusals, captured 0x02/0x09 decoding, control frames) are pinned through the real CN105Climate on both 32-bit variants using real frames only; two guard log lines turn out to be dead code and are pinned as such.**

## Performance

- **Duration:** ~45 min (start time not captured; estimated)
- **Completed:** 2026-10-29
- **Tasks:** 3 (1 tracer + 2 auto)
- **Files:** 1 created (test file only), 778 lines, 17 `TEST_F`

## Accomplishments

- 17 tests selected by `ProfileState*` on both variants (9 heat pump, 8 Lossnay); full harness run: ESP32 101/101, ESP8266 102/102.
- Tracer gate (automated-only verify, end-of-phase mode): Task 1 verify passed on both variants before expanding.
- Every frame fed is a `real_frames::` constant; expected written frames are test_lossnay.cpp checksums or the component's own encoder layout. No `SYNTHETIC` label, `components/cn105` unchanged.

## Task Commits

1. **Task 1 (tracer): real 0x02 frame from bytes to HA state** - `3262278` (test)
2. **Task 2: HeatPump guards, grace windows, action/stage, dual setpoint, persistence** - `c58ece1` (test)
3. **Task 3: Lossnay profile state handling** - `bc7a300` (test)

## Observations pinned (characterization rule: asserted as observed, contradictions with the plan reading reported)

- **Dead log: "Ignoring setpoint: pending user temp".** `shouldApplyIncomingSetpoint()` starts with `if (wantedSettings.temperature != -1) return false;`, which returns silently before `hasPendingUserTemperature()` (which needs the same field != -1) is reached. The incoming setpoint IS ignored while a user temperature is pending, but with no log. Pinned in `Characterize_IncomingSetpointIgnoredWhilePendingUserTemp` (positive check that debug logs are captured, then `EXPECT_FALSE(contains(...))`).
- **Dead log: "Ignoring setpoint: post-send grace".** `isWithinPostSendGrace()` needs `hasBeenSent`, but `sendWantedSettingsDelegate()` sets it true and `resetSettings()` clears it in the same call, so the grace window is never open through the real flow. What protects the user's value after a send is the 3000 ms received-setpoint grace. `Characterize_IncomingSetpointIgnoredWithinPostSendGrace` pins both facts: closed after a real send, and the helper's own arithmetic by forcing `hasBeenSent` (open below lastChange + 1750 ms, closed at 1750 ms).
- **Polling is starved while a command is pending.** In `loop()`, `wantedSettings.hasChanged && !isCycleRunning()` takes the `checkPendingWantedSettings()` branch, so no new poll cycle starts until the command is sent. A received 0x02 can meet a pending command only if the command lands while a cycle is already in flight; the tests use `poll_with_hook` for that. This matters for Phase 6.
- **Received-setpoint grace:** `Ignoring setpoint: disagrees with user (21.5 vs 24.0)` inside 3000 ms of the command (strict `<`, 2999 ignored / 3000 applied); afterwards the heat pump's 21.5 replaces the user's 24 and is published.
- **Vane grace has no log line:** an incoming vane differing from the last user vane inside 3000 ms keeps `currentSettings.vane` and the optimistic swing mode (VERTICAL); the same frame after 3000 ms sets vane "up" and swing OFF.
- **Action from stage:** with `use_stage_for_operating_status`, real POWER_LOW (stage LOW) turns an idle HEAT unit into HEATING even though 0x06 still reports not operating; without the fallback the same frame leaves IDLE. No captured 0x06 frame has the operating byte set.
- **Dual setpoint:** seeding in HEAT with a two-point trait gives low 21.5 / high 22.0 (not 21.5): `update_action()` runs `sanitize_setpoints()` before the setpoint is reconciled and seeds the 18.0-22.0 defaults. A HEAT_COOL band 20/24 is sent as hardware AUTO with a single setpoint (room 23.0 inside the band -> byte 19 = 0xAE). A high-only change within 600 ms of a low change is ignored by the UI anti-rebound; after 700 ms it applies.
- **Persistence:** the band is saved at `control()` time, restored by `restore_setpoints()` (mode back to HEAT_COOL, 20/24, no write); a record saved in HEAT mode is "not a restorable HEAT_COOL band" and nothing is restored.
- **Lossnay unanswered requests:** with only a 0x02 answer, the cycle requests 0x02 then 0x03 and ends by timeout after 3 s (no 0x06/0x09/0x04 request). A 0x09 answer given to the 0x03 request moves the cycle on to 0x04 (the scheduler dispatches by response code).
- **Lossnay temperature:** the first call logs "Ignoring Lossnay target-temperature changes: no confirmed setpoint field" once per instance; other features log "Ignoring unsupported Lossnay operation (N)" every time (N = enum value 1..4). A temperature call sets no wanted value, publishes nothing and sends nothing.
- **Lossnay control frames:** OFF -> checksum 0x78, HEAT (always with power ON) -> 0x73, fan HIGH -> 0x6D, equal to the test_lossnay.cpp `CapturedWriteChecksums` values. FAN_ONLY, AUTO and fans LOW/MEDIUM/MIDDLE have no capture; their frames are the encoder layout (flags in byte 6, power byte 8, mode byte 10, fan byte 11).

## Deviations from Plan

### Plan reading contradicted by observation (characterization rule)

**1. [Observation] Two of the three "Ignoring setpoint" guard logs cannot be produced by the real flow**
- **Found during:** Task 2
- **Issue:** the plan expects the three logs "pending user temp", "post-send grace" and "disagrees with user" to be asserted as present. Only the third can be reached (see Observations).
- **Fix:** the two dead ones are asserted absent with a positive control that the debug log level is captured; the post-send helper is additionally pinned by forcing `hasBeenSent`. The acceptance grep (`grep -c 'Ignoring setpoint' >= 3`) still passes (6 matches).
- **Files modified:** tests/unit/characterization/test_profile_state.cpp
- **Commit:** `c58ece1`

**2. [Interface adjustment] `connect()` of the fixture cannot be used in Lossnay mode; handleConnectionSuccess() alone was not enough**
- **Found during:** Task 3
- **Issue:** `Cn105Fixture::connect()` asserts the heat-pump CONNECT bytes and feeds a profile-0x30 reply. Calling `hp().handleConnectionSuccess()` alone left `lastResponseMs` unset, so the first poll request was refused ("could not write as asked, because UART is not connected").
- **Fix:** the Lossnay helper feeds the real CONNECT_REPLY (rejected by the profile, as pinned; it still stamps `lastResponseMs` like any received frame) and then calls the production `handleConnectionSuccess()`. The plan's fallback `setHeatpumpConnected(true)` would not have helped. No frame is invented.
- **Files modified:** tests/unit/characterization/test_profile_state.cpp
- **Commit:** `bc7a300`

**3. [Interface adjustment] HeatPump dual-setpoint fixture is a ProfileState subclass, and the command-meets-answer scenario needs a hook**
- The plan's `set_debounce_delay` idea (keep the command pending while a 0x02 arrives) does not work because loop() starves polling while a command waits (see Observations); `poll_with_hook` places the command between request and answer instead. `ClimateControlsHeatCool`'s HEAT_COOL traits alone do not enable two-point targets, so the dual-setpoint fixture also adds `CLIMATE_REQUIRES_TWO_POINT_TARGET_TEMPERATURE`.

**Total deviations:** 3 (1 observation contradicting the plan reading, 2 interface adjustments), none touching `components/cn105`.

## Wanted captures

For plan 01-12 to turn into the capture request (no frame was invented in this plan):

1. **Lossnay handshake reply.** Scenario: a Lossnay unit answering the CONNECT `FC 5A 01 34 02 CA 01 A4` (profile 0x34). Expected frame: `FC 7A 01 34 ...` (command 0x7A, profile byte 0x34, which `LossnayProtocol::accepts_handshake` requires). Needed to replace the `handleConnectionSuccess()` shortcut in `ProfileStateLossnay`.
2. **Lossnay 0x03 / 0x06 / 0x04 info replies** (room temperature, status, error info) from a Lossnay unit. Today an unanswered 0x03 ends every Lossnay cycle by timeout after 3 s, so the 0x09 captures can only be fed as the answer to the 0x03 request; with real replies the cycle would reach 0x09 naturally and the status decode (`decode_status`, power/energy) could be pinned.
3. **A heat-pump 0x06 reply with the operating byte set** (compressor running, HEAT). Needed to pin the HEATING/COOLING action without the stage fallback; today only STATUS_IDLE exists.
4. **A heat-pump 0x02 reply in AUTO mode (mode byte 0x08)** and one with a HEAT_COOL-compatible state, to pin the real-frame reconciliation of a HEAT_COOL band after a poll (this plan polls only HEAT frames, so the "band preserved when hardware reports HEAT/COOL/AUTO" claim of `restore_setpoints()` is not exercised through a frame).

## Issues Encountered

- None blocking. The first draft of the pending-command test failed (no poll cycle ran while the command waited); this exposed the polling starvation recorded above.

## Verification Results

- `tests/unit/run-harness.sh --docker --variant all --filter 'ProfileState*'`: esp32 17/17... see note: the filter selects 9 `ProfileState*` heat pump tests plus 8 `ProfileStateLossnay` tests = 17 per variant; `[run-harness] OK: esp32 esp8266`.
- `--filter 'ProfileStateLossnay.*'`: 8 per variant, OK. Full suite: ESP32 101, ESP8266 102, all pass.
- `git diff --quiet origin/main -- components/cn105`: clean (D-03).
- `! grep -n SYNTHETIC test_profile_state.cpp`: no match. `grep -c LOSSNAY_` = 14, `grep -c 'real_frames::CONNECT_REPLY'` = 2, `grep -c 'Ignoring setpoint'` = 6.
- Every `feed()` argument is a `real_frames::` constant or a responses-map entry built from them.

## User Setup Required

None (Docker needed locally for `--docker`).

## Known Stubs

None.

## Threat Flags

None. T-01-19 (no invented Lossnay handshake frame) and T-01-20 (components/cn105 untouched) mitigated as verified above.

## Next Phase Readiness

- Ready for 01-12 (capture request, using the Wanted captures list above) and for Phases 6 and 8, which rework exactly this state handling: the dead guard logs, the `hasBeenSent` reset, the polling starvation and the Lossnay cycle behaviour are the pins that will flip.

## Self-Check: PASSED

- File exists: tests/unit/characterization/test_profile_state.cpp.
- Commits `3262278`, `c58ece1`, `bc7a300` are ancestors of HEAD.
