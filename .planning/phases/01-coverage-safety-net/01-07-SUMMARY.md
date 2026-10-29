---
phase: 01-coverage-safety-net
plan: 07
subsystem: testing
tags: [googletest, characterization, cycle-management, request-scheduler, bug-01, bug-07, m32, esp32, esp8266]

requires:
  - phase: 01-coverage-safety-net
    provides: "01-01..01-03 fixture API (ClockFixture, Cn105Fixture), TestClock/TestLog, ESP32 + ESP8266 -m32 harness variants, run-harness.sh"
provides:
  - "CycleManagement / CycleManagementComponent suites: strict interval and timeout boundaries, 1500 ms deferral, defer surviving cycleEnded, BUG-01 wrap pins at struct and component level"
  - "RequestScheduler / RequestSchedulerComponent / RequestSchedulerHvacOptions suites: chain order, skips, soft timeouts, registered request table, BUG-07 stall and late-response pins"
affects: [01-08, phase-04-time-wrap, phase-05-link-stall]

actuals:
  tokens: 8900
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Standalone RequestScheduler tests use a Recorder of std::function callbacks; the context callback returns nullptr so canSend/onResponse are skipped"
    - "Component-level stall tests drive the real loop with a pump() helper that answers FC 42 requests and records (clock, code)"

key-files:
  created:
    - tests/unit/characterization/test_cycle_management.cpp
    - tests/unit/characterization/test_request_scheduler.cpp
  modified: []

key-decisions:
  - "Late/duplicate 0x03 response while 0x06 is awaited is pinned as a duplicated 0x06 request frame (LINK-04), observed on the real component"
  - "An extra RequestSchedulerHvacOptions fixture attaches an HVACOptionSwitch so the canSend-gated 0x42 request and its 500 ms soft timeout are exercised"

patterns-established:
  - "Bug pins are named Characterize_BUG<NN>_... with '// Pins current buggy behaviour — flip in Phase N (REQ-ID)' directly above"

requirements-completed: [COV-04]

coverage:
  - id: D1
    description: "BUG-01 pinned end to end on both variants: after a cycle completes just before the 32-bit wrap hasUpdateIntervalPassed stays false after the wrap, the cycle never times out, a defer just before the wrap is lost, and the real loop stops starting FC 42 cycles for a minute after the wrap"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "CycleManagement.Characterize_BUG01_StallsAfterMillisWrap, ..._CycleNeverTimesOutAcrossWrap, ..._DeferJustBeforeWrap; CycleManagementComponent.Characterize_BUG01_ComponentStopsPollingAfterWrap (test_cycle_management.cpp)"
        status: pass
    human_judgment: false
  - id: D2
    description: "cycle_management timing semantics pinned: strict '>' interval, '> 2*interval+1000' timeout, 1500 ms defer (VERY_VERBOSE build), defer surviving cycleEnded, checkTimeout ends and logs"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "CycleManagement.Characterize_HasUpdateIntervalPassedIsStrict, ..._CycleTimeoutBoundary, ..._DeferCycleSetsDeadline1500ms, ..._CycleEndedKeepsFutureDeadline, ..._CheckTimeoutEndsCycleAndLogs, ..._InitClearsRunningAndStampsNow; CycleManagementComponent.Characterize_NextCycleStartsAfterInterval"
        status: pass
    human_judgment: false
  - id: D3
    description: "RequestScheduler behaviour pinned: registration order is cycle order, disabled/interval/timer_bypass/millis-zero skips, terminate when exhausted, unknown code ignored, soft-timeout failure counting and disabling, stale timeout ignored"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "RequestScheduler.Characterize_* (14 tests, test_request_scheduler.cpp)"
        status: pass
    human_judgment: false
  - id: D4
    description: "BUG-07 pinned: zero soft timeout schedules nothing, missing 0x02 response stalls until the cycle timeout then a new cycle, late response restarts the chain / duplicates the next request"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "RequestScheduler.Characterize_BUG07_ZeroSoftTimeoutSchedulesNothing, ..._BUG07_LateResponseRestartsChain; RequestSchedulerComponent.Characterize_BUG07_MissingResponseStallsUntilCycleTimeout, ..._BUG07_LateResponseDuplicatesChain"
        status: pass
    human_judgment: false
  - id: D5
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
plan_head_before: c5801025efc5e0d654bf2c8dc89bf03fb7ef42db
plan_head_after: b2d1e20d4281bc26298e9c692d0339cbbe908ba4
commits: 3
---

# Phase 1 Plan 07: cycle_management and request_scheduler characterization Summary

**The polling timing and the info-request chain of the unmodified component are pinned on both the ESP32 and ESP8266 32-bit variants, including the millis() wrap stall (BUG-01) and the missing-timeout / late-response chain bugs (BUG-07), so Phases 4 and 5 will flip named tests instead of changing behaviour silently.**

## Performance

- **Duration:** ~35 min (start time not captured; estimated)
- **Completed:** 2026-10-29
- **Tasks:** 3
- **Files:** 2 created (test files only)

## Accomplishments

- `test_cycle_management.cpp`: 9 struct-level `CycleManagement` tests and 2 `CycleManagementComponent` tests (start clock 0xFFFF0000). The component test runs the real `loop()` through the wrap and shows no FC 42 cycle starts for 60 s afterwards, after 30+ normal cycles before it.
- `test_request_scheduler.cpp`: 14 standalone `RequestScheduler` tests with a recording callback harness, 4 `RequestSchedulerComponent` tests (request table, 0x42 skipped without switches, BUG-07 stall, BUG-07 duplicated request) and 1 `RequestSchedulerHvacOptions` test (0x42 with an air purifier switch, soft timeout fires).
- Full harness run is green: `tests/unit/run-harness.sh --docker --variant all` gives ESP32 45/45 and ESP8266 44/44.

## Task Commits

1. **Task 1 (tracer): BUG-01 wrap stall on both variants** - `2f5e866` (test)
2. **Task 2: cycle_management catalog and component-level wrap stall** - `e609fff` (test)
3. **Task 3: RequestScheduler catalog and BUG-07 pins** - `b2d1e20` (test)

Tracer gate (automated-only verify, end-of-phase mode): Task 1 verify re-run end to end (1 test per variant, `[run-harness] OK`) before expanding.

## Observed values worth knowing (pinned)

- Cycle timeout fires 3001-3020 ms after the cycle start (tick granularity 10 ms); the next 0x02 request follows the timeout by 1001-1030 ms.
- Normal cadence: gap between two 0x02 requests is more than 1000 ms and at most 1100 ms at a 1000 ms interval.
- `deferCycle()` just before the wrap (clock 0xFFFFFF00) stores 1244 (0xFFFFFF00 + 1500 wrapped) and `hasUpdateIntervalPassed(1000)` is true 1 ms later (the deferral is lost); once the clock has wrapped the stamp acts as a deadline again until 1244 + 1000.
- A late 0x03 response while 0x06 is awaited produces a second FC 42 request for 0x06 on the wire.
- With an air purifier switch attached and no 0x42 reply, the cycle order is 0x02 0x03 0x06 0x09 0x42 0x04 and 0x42 gets `failures == 1` from its 500 ms soft timeout.

## Deviations from Plan

None - plan executed as written. No observation contradicted the production-code reading quoted in the plan.

Minor process note: the per-plan commit ledger (`gsd-plan-head-before-01-07`) was created right after the first task commit instead of before it, from `HEAD~1` (= the orchestrator-provided base `c580102`), so `commits: 3` and `plan_head_before` are measured against the correct base.

## Real captures needed

- No real CN105 capture was required for the assertions above (the default responses come from existing real frames).
- A real **0x42 (HVAC options) response** capture is missing: `RequestSchedulerHvacOptions` can only exercise the unanswered path. Needed scenario: unit with air purifier / night mode / circulator, one info request 0x42 and its reply while the switches are toggled, before any phase changes the 0x42 decode.

## Issues Encountered

None.

## Verification Results

- `tests/unit/run-harness.sh --docker --variant all --filter 'CycleManagement*'`: 11 tests per variant, `[run-harness] OK: esp32 esp8266`.
- `tests/unit/run-harness.sh --docker --variant all --filter 'RequestScheduler*'`: 19 tests per variant, `[run-harness] OK: esp32 esp8266`.
- Acceptance greps: `Characterize_BUG01_` x5, `flip in Phase 4` x4, `Characterize_BUG07_` x4, `flip in Phase 5 (LINK-0[34])` x4, `using namespace esphome;` x0.
- `git diff --quiet origin/main -- components/cn105`: clean (D-03).

## User Setup Required

None.

## Known Stubs

None.

## Threat Flags

None. T-01-13 (greppable bug-pin naming and flip comments) and T-01-14 (no production edit) mitigated as verified above.

## Next Phase Readiness

- Phase 4 flips the four `Characterize_BUG01_*` tests (TIME-01/TIME-02); Phase 5 flips the four `Characterize_BUG07_*` tests (LINK-03/LINK-04).

## Self-Check: PASSED

- Files exist: `tests/unit/characterization/test_cycle_management.cpp`, `tests/unit/characterization/test_request_scheduler.cpp`.
- Commits `2f5e866`, `e609fff`, `b2d1e20` are ancestors of HEAD.
