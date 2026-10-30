---
phase: 01-coverage-safety-net
plan: 15
subsystem: testing
tags: [github-actions, ci, coverage, gcovr, ratchet, pull-request, stacked-pr]

requires:
  - phase: 01-coverage-safety-net
    provides: "PR 1a baseline and CI (01-11); characterization of remote temperature, connection lifecycle, read path, entities, installer functions and Lossnay (01-12..01-14)"
provides:
  - "tests/unit/coverage-baseline.json ratcheted from the first PR 1b ubuntu-24.04 CI run (line 84.87%, branch 73.84%)"
  - "Draft PR 1b (#4) on coredmp95/MitsubishiCN105ESPHome, stacked on PR 1a (#2), with a fully green CI run"
  - "Phase 1 gate confirmed on CI: baseline CI-derived, bug pins present, components/cn105 untouched"
affects: [phase-02]

actuals:
  tokens: 40
  tasks: 2
  commits: 1
plan_head_before: 6f6e25a0f71d9a991b81ef51befc9b35326e9bc8
plan_head_after: 3dea145c863d035466e80948d332777932956b14
commits: 1

tech-stack:
  added: []
  patterns:
    - "Stacked PR: PR 1b targets the PR 1a head branch until PR 1a merges, then is retargeted to main"
    - "Baseline produced by coverage_gate.py propose from the downloaded CI artifact, committed alone"

key-files:
  created: []
  modified:
    - tests/unit/coverage-baseline.json

key-decisions:
  - "PR 1b opened as a draft, base gsd/phase-01-coverage-safety-net (human answer: authorized: stacked); the workflow's plain pull_request trigger has no base-branch filter so CI ran on the stacked PR"
  - "The branch was pushed with an explicit branch name only; gsd/phase-01-coverage-safety-net (PR 1a head, remote 6911e8b) was never pushed to"

patterns-established:
  - "When two runs overlap on a PR, the first run (provisional-to-ratchet proposal) still completes; the artifact of the first run is the source of the baseline"

requirements-completed: [COV-02, COV-04]

coverage:
  - id: D1
    description: "Ratcheted CI-derived baseline: line 67.05% -> 84.87% (2648/3120), branch 60.08% -> 73.84% (1578/2137), source names CI run 41010865084, toolchain gcc 13.3.0 / gcovr 8.6 / ubuntu 24.04"
    requirement: COV-02
    verification:
      - kind: command
        ref: "python3 tests/unit/tools/coverage_gate.py ratchet --summary <artifact>/summary.json --baseline tests/unit/coverage-baseline.json -> OK (+0.00 / +0.00); baseline json assertion prints ok"
        status: pass
    human_judgment: false
  - id: D2
    description: "PR 1b final CI run green: unit-tests, coverage-gate (ratchet OK, diff NO_LINES), esphome-build 7/7, exactly one sticky coverage comment"
    requirement: COV-04
    verification:
      - kind: other
        ref: "run 41011818339 success (9/9 jobs); comment 6516094223 is the only one with the cn105-coverage-report marker"
        status: pass
    human_judgment: false
  - id: D3
    description: "Phase gate: every pinned bug is greppable and shipped code is unchanged"
    requirement: COV-04
    verification:
      - kind: command
        ref: "for b in 01 02 03 04 05 07 08: grep -rq Characterize_BUG<b>_ tests/unit/characterization -> all present; git diff --quiet origin/main -- components/cn105 -> exit 0"
        status: pass
    human_judgment: false

duration: 55min
completed: 2026-10-30
status: complete
---

# Phase 1 Plan 15: Close PR 1b with a ratcheted CI baseline Summary

**Draft PR 1b (#4) is open on the fork, stacked on PR 1a, and its baseline is ratcheted to the CI-measured 84.87% lines / 73.84% branches with a fully green CI run.**

## Performance

- **Duration:** about 55 min (mostly waiting for CI)
- **Completed:** 2026-10-30
- **Tasks:** 2 (Task 1 was the human authorization "authorized: stacked", answered before this run)
- **Files modified:** 1 (`tests/unit/coverage-baseline.json`)

## Accomplishments

- Created `gsd/phase-01b-characterization-extension` at 6f6e25a, pushed it to `origin` (verified `git@github.com:coredmp95/MitsubishiCN105ESPHome.git`) and opened draft PR 1b: https://github.com/coredmp95/MitsubishiCN105ESPHome/pull/4 (base `gsd/phase-01-coverage-safety-net`, state OPEN, `isDraft` true). The body notes the stack on #2, the retarget to main after #2 merges, the "Wanted captures" list and the 01-12 existing-frames-only decision.
- Baseline regenerated with `coverage_gate.py propose` from the `coverage-report` artifact of the first run and committed alone as `3dea145`; ratchet against that summary exits OK.
- Second run green end to end; the sticky comment was edited in place (not duplicated).

## CI evidence

| Run | Head | Verdict |
|-----|------|---------|
| [41010865084](https://github.com/coredmp95/MitsubishiCN105ESPHome/actions/runs/41010865084) | 6f6e25a (PR 1a baseline 67.05 / 60.08) | success. unit-tests ok; coverage-gate = **RATCHET**: line 84.87% (2648/3120) +17.82, branch 73.84% (1578/2137) +13.76; diff = **NO_LINES**; esphome-build 7/7 |
| [41011818339](https://github.com/coredmp95/MitsubishiCN105ESPHome/actions/runs/41011818339) | 3dea145 (ratcheted baseline) | success (9/9 jobs: Unit Tests, Coverage gate **OK** line +0.00 / branch +0.00, esphome-build 7/7); diff NO_LINES |

PR comment: id 6516094223, the only one with `<!-- cn105-coverage-report -->`, created 05:57:33Z, edited in place at 06:08:04Z (links to run 41011818339).

### Baseline PR 1a -> PR 1b

| | PR 1a (run 40960306032) | PR 1b (run 41010865084) |
|---|---|---|
| Lines | 67.05% (2088/3114) | 84.87% (2648/3120) |
| Branches | 60.08% (1284/2137) | 73.84% (1578/2137) |

Both values are above the PR 1a ones (acceptance criterion).

### Pinned bugs (`grep -rn Characterize_BUG tests/unit/characterization`, by bug ID)

- **BUG-01:** `Characterize_BUG01_ComponentStopsPollingAfterWrap`, `_CycleNeverTimesOutAcrossWrap`, `_DeferJustBeforeWrap`, `_StallsAfterMillisWrap`
- **BUG-02:** `Characterize_BUG02_GapAboveOneDegreeIsDetected`, `_MarginUsesIntegerAbs`
- **BUG-03:** `Characterize_BUG03_NoRetryOfClearedWantedSettings`, `_OptimisticPublishBeforeSend`, `_RunStatesPublishedAndClearedAfterQueuedWrite`, `_WantedSettingsPublishedAndClearedAfterQueuedWrite`
- **BUG-04:** `Characterize_BUG04_EachFailedWriteTriggersReconnect`, `_InfoRequestOverwritesPendingCommand`
- **BUG-05:** `Characterize_BUG05_LowLevelReinitTargetsUart0Pins1And2`
- **BUG-07:** `Characterize_BUG07_LateResponseDuplicatesChain`, `_LateResponseRestartsChain`, `_MissingResponseStallsUntilCycleTimeout`, `_ZeroSoftTimeoutSchedulesNothing`
- **BUG-08:** `Characterize_BUG08_ConnectionActiveSignedOverflow`, `_DebounceSignedNowPast2Pow31`

(BUG-06 is not pinned by design of the plan: the requirement list is 01, 02, 03, 04, 05, 07, 08.)

### Wanted captures still open (`tests/unit/README.md`, "Wanted captures")

1. `0x42` HVAC-options reply (air purifier, night mode, circulator)
2. `0x20` / `0x22` functions replies
3. `0x61` ACK after a `0x41` SET and after the remote-temperature packet (replaces the synthetic ACK, D-23)
4. `0x09` reply during defrost (DEFROST sub-mode)
5. `0x7B` installer-mode connect reply to `0x5B` (replaces the synthetic reply, D-23)
6. Lossnay handshake reply to CONNECT `0x5A` with profile `0x34`
7. `0x41` SET with a `wideVane` change (horizontal swing)
8. `0x41` SET in Fahrenheit mode
9. Lossnay `0x03`, `0x06`, `0x04` info replies
10. Heat-pump `0x06` reply with the operating byte set (compressor running, HEAT)
11. Heat-pump `0x02` reply in AUTO mode and a HEAT_COOL-compatible state
12. `0x04` error-info reply with a real non-zero error code

Per the 01-12 decision, PR 1b proceeded with the existing frames only; none of these blocks the phase.

## Task Commits

1. **Task 1: Authorize the PR 1b push and PR** - answered by the human ("authorized: stacked"); branch creation, push and draft PR #4 executed in this run
2. **Task 2: Ratchet baseline and confirm the phase gate** - `3dea145` (test)

**Plan metadata:** committed with this SUMMARY.

## Files Created/Modified

- `tests/unit/coverage-baseline.json` - baseline from CI run 41010865084 (ubuntu-24.04, gcc 13.3.0, gcovr 8.6, ESPHome 2026.10.0)

## Decisions Made

- PR 1b is a draft stacked on PR 1a (#2); it must be retargeted to `main` after #2 merges.
- The ratchet used the first run's artifact, not a local run (local coverage matched: about 84.9% / 73.8%).

## Deviations from Plan

None - plan executed as written. The local branch `gsd/phase-01-coverage-safety-net` was never pushed (PR 1a unchanged on the remote at 6911e8b); the PR 1b branch carries the 16 commits of plans 01-12 to 01-14 (and the harness determinism fix) on top of it.

## Issues Encountered

- The first run (6f6e25a) was still running its esphome-build jobs when the baseline commit was pushed; it was not cancelled and finished green, so both runs are reported.

## Known Stubs

None.

## Threat Flags

None. The only new surface is the draft PR on the fork; no upstream (echavet) operation was performed, no force-push, no merge.

## User Setup Required

None.

## Next Phase Readiness

- Phase 1 plans are complete (15/15). PR 1a (#2) and PR 1b (#4) are drafts with green CI; the user marks them ready and merges #2 first, then retargets #4 to `main`.
- Open items for the user: the visual check of the job summary and HTML report in the GitHub UI (from 01-11), and the optional real captures listed above.
- Phase 2 can use `tests/unit/coverage-baseline.json` (84.87 / 73.84) as the floor.

## Self-Check: PASSED

- `tests/unit/coverage-baseline.json` has source "CI run 41010865084 (ubuntu-24.04)", no provisional flag; `coverage_gate.py ratchet` exits 0 (OK).
- Commit 3dea145 is an ancestor of HEAD and present on `origin/gsd/phase-01b-characterization-extension`.
- `git diff --quiet origin/main -- components/cn105` exits 0; every required BUG pin is present.
- Latest Build run on the PR 1b branch concluded success; one sticky comment on PR 4; PR 4 state OPEN, base `gsd/phase-01-coverage-safety-net`.

---
*Phase: 01-coverage-safety-net*
*Completed: 2026-10-30*
