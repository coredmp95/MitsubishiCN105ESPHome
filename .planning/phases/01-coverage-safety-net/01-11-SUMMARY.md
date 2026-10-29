---
phase: 01-coverage-safety-net
plan: 11
subsystem: testing
tags: [github-actions, ci, coverage, gcovr, ratchet, diff-coverage, pull-request]

requires:
  - phase: 01-coverage-safety-net
    provides: "coverage-gate CI job, ci_coverage_gate.sh, coverage_gate.py (01-04, 01-06); characterization tests (01-07..01-10)"
provides:
  - "tests/unit/coverage-baseline.json derived from the first ubuntu-24.04 CI run (line 67.05%, branch 60.08%), no provisional flag"
  - "Draft PR 1a (#2) on coredmp95/MitsubishiCN105ESPHome with a fully green CI run (unit-tests, coverage-gate, esphome-build 7/7)"
  - "Label coverage-exempt on the fork"
  - "Live proof of D-13 and both D-14 outcomes on a throwaway PR (closed, branch deleted)"
affects: [01-12, 01-15]

actuals:
  tokens: 150
  tasks: 3
  commits: 1
plan_head_before: 295f3e9c9c525a00704178a34dd01d57424768f9
plan_head_after: b471b237c11ab0fd5cc122163b85b40dc5d5449b
commits: 1

tech-stack:
  added: []
  patterns:
    - "Baseline is produced only by coverage_gate.py propose from the downloaded CI artifact and committed alone (CI never writes it)"
    - "Throwaway gate-demonstration PR based on the PR 1a head branch, edited in a scratch git worktree, deleted afterwards"

key-files:
  created: []
  modified:
    - tests/unit/coverage-baseline.json

key-decisions:
  - "The .planning/ commits stay in PR 1a (user answer to Task 1): the phase branch was pushed as is"
  - "The probe PR was based on the PR 1a head branch (not main), so its diff contained only the probe lines"
  - "The probe's third run (ratchet FAIL) was cancelled after the Coverage gate job failed, to save runner time; its verdict is read from the gate job and the PR comment"

patterns-established:
  - "gh pr edit --add-label fails on this repo (Projects classic GraphQL deprecation); use gh api -X POST repos/<repo>/issues/<n>/labels instead"
  - "gh run rerun --failed only works once the whole run has completed; the label must be added, then the run awaited"

requirements-completed: [COV-01, COV-02, COV-03, COV-04]

coverage:
  - id: D1
    description: "CI-derived baseline committed: line 67.05% (2088/3114), branch 60.08% (1284/2137), toolchain gcc 13.3.0 / gcovr 8.6 / ubuntu 24.04, source names CI run 40960306032"
    requirement: COV-02
    verification:
      - kind: command
        ref: "python3 tests/unit/tools/coverage_gate.py ratchet --summary <artifact>/summary.json --baseline tests/unit/coverage-baseline.json -> OK, exit 0; baseline json assertion prints ok"
        status: pass
    human_judgment: false
  - id: D2
    description: "PR 1a CI is green and publishes coverage in the job summary, the coverage-report artifact and exactly one PR comment edited in place"
    requirement: COV-01
    verification:
      - kind: other
        ref: "run 40961867696 success (9/9 jobs); one comment with the cn105-coverage-report marker (id 6509100457), created 21:48:10Z, edited 21:59:45Z"
        status: pass
    human_judgment: false
  - id: D3
    description: "Gate demonstrated live on a throwaway PR: diff coverage below 80% fails, coverage-exempt label exempts it while still showing the percentage, a baseline above the measured value fails the ratchet"
    requirement: COV-03
    verification:
      - kind: other
        ref: "probe runs 40962356436 (diff FAIL then EXEMPT after rerun) and 40964241689 (ratchet FAIL)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Job summary rendering and HTML report as seen in the GitHub UI"
    requirement: COV-01
    verification: []
    human_judgment: true
    rationale: "Rendering of the job summary and download of the HTML report are visual checks in the GitHub UI (the plan's human-check)"

duration: 40min
completed: 2026-10-29
status: complete
---

# Phase 1 Plan 11: Close PR 1a with CI evidence Summary

**Draft PR 1a is open on the fork with a CI-derived coverage baseline (line 67.05%, branch 60.08%), a fully green CI run, one sticky coverage comment edited in place, and the diff-coverage and ratchet rules proven on real runs.**

## Performance

- **Duration:** about 40 min (mostly waiting for CI)
- **Completed:** 2026-10-29
- **Tasks:** 3 (Task 1 was the human authorization, answered before this run; its operations were performed here)
- **Files modified:** 1 (`tests/unit/coverage-baseline.json`)

## Accomplishments

- Pushed the phase branch to `origin` (verified `git@github.com:coredmp95/MitsubishiCN105ESPHome.git`), opened draft PR 1a as https://github.com/coredmp95/MitsubishiCN105ESPHome/pull/2 (base `main`, head `gsd/phase-01-coverage-safety-net`, `isDraft` true), created label `coverage-exempt`.
- Baseline taken from the CI artifact of the first run and committed alone; the second run is green and the sticky comment was updated in place.
- Both ratchet outcomes and the diff rule demonstrated once on a throwaway PR, then cleaned up.

## CI evidence

### PR 1a (#2)

| Run | Head | Verdict |
|-----|------|---------|
| [40960306032](https://github.com/coredmp95/MitsubishiCN105ESPHome/actions/runs/40960306032) | 295f3e9 (provisional baseline 0.0) | success (9/9 jobs). unit-tests published the report; coverage-gate = **RATCHET**, proposing line 67.05% (2088/3114) and branch 60.08% (1284/2137) (live proof that a rise allows ratcheting, D-14); diff = **NO_LINES** |
| [40961867696](https://github.com/coredmp95/MitsubishiCN105ESPHome/actions/runs/40961867696) | b471b23 (CI baseline) | success (9/9 jobs: unit-tests, coverage-gate **OK** line +0.00 / branch +0.00, esphome-build 7/7); diff NO_LINES |

PR comment: id 6509100457 (the only comment with `<!-- cn105-coverage-report -->`), created 21:48:10Z, edited in place at 21:59:45Z (it links to run 40961867696).

### Throwaway probe PR (#3, closed, `tmp/coverage-gate-probe` deleted)

Probe branch based on b471b23; commit 1 added a never-called 4-line function to the end of `components/cn105/utils.cpp` (4 coverable lines, 513-516), commit 2 raised the baseline line value from 67.05 to 72.05.

| # | Run | Verdict |
|---|-----|---------|
| 1 | [40962356436](https://github.com/coredmp95/MitsubishiCN105ESPHome/actions/runs/40962356436) attempt 1 | Coverage gate **FAIL**: ratchet OK (66.97%, -0.08, within the 0.1 tolerance) but **Diff coverage: FAIL** 0.00% (0/4) below 80% |
| 2 | same run, attempt 2 (label added, `gh run rerun --failed`) | Coverage gate **success**: **Diff coverage: EXEMPT**, still showing "measured 0.00% (0/4 changed lines covered)" |
| 3 | [40964241689](https://github.com/coredmp95/MitsubishiCN105ESPHome/actions/runs/40964241689) (label kept) | Coverage gate **FAIL**: line **FAIL** 66.97% vs baseline 72.05% (-5.08), ratchet FAIL even though diff is EXEMPT. Run cancelled afterwards (esphome-build jobs not needed) |

Probe branch commits (now deleted): 79f67a0, 2940b1d.

## Task Commits

1. **Task 1: Authorize pushes/PR/label/probe** - answered by the human via the orchestrator; operations executed in this run (push, draft PR #2, label, later push, probe PR #3)
2. **Task 2: CI-derived baseline** - `b471b23` (test)
3. **Task 3: Live gate demonstration** - no phase-branch commit by design (probe lived only on the deleted branch)

**Plan metadata:** committed with this SUMMARY.

## Files Created/Modified

- `tests/unit/coverage-baseline.json` - baseline from CI run 40960306032 (ubuntu-24.04, gcc 13.3.0, gcovr 8.6, ESPHome 2026.10.0)

## Decisions Made

- `.planning/` commits stay in PR 1a (user decision); the PR description says so.
- Probe PR targeted the PR 1a head branch so its diff was only the probe.
- Third probe run cancelled once the Coverage gate job had failed.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `gh pr edit --add-label` unusable**
- **Found during:** Task 3
- **Issue:** `gh pr edit` failed with the Projects (classic) GraphQL deprecation error.
- **Fix:** added the label through the REST API (`gh api -X POST repos/coredmp95/MitsubishiCN105ESPHome/issues/3/labels -f 'labels[]=coverage-exempt'`), same effect, same repository.
- **Files modified:** none
- **Committed in:** n/a

**2. [Rule 3 - Blocking] Failed job could not be re-run while the run was in progress**
- **Found during:** Task 3
- **Issue:** the probe run's esphome-build jobs were still queued/running, so the Coverage gate job could not be re-run.
- **Fix:** waited for the run to complete (conclusion failure from the Coverage gate only), then `gh run rerun --failed`.
- **Files modified:** none

---

**Total deviations:** 2 (both Rule 3, tooling workarounds, no repository change)
**Impact on plan:** none; every demonstration the plan asked for was observed.

## Issues Encountered

- Reproduction note: the plan's `utils.cpp` probe location, taken at the end of the file after the closing `#endif` of the `TEST_MODE` block, is compiled in all coverage variants (4 coverable lines), so it measured as a real uncovered diff.
- Ratchet tolerance observed live: the probe's -0.08 line delta passed (OK) in runs 1 and 2, consistent with the 0.1 point tolerance (D-14).

## Known Stubs

None.

## Threat Flags

None. The only new surface is the draft PRs and label on the fork; no upstream (echavet) operation was performed.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- PR 1a (https://github.com/coredmp95/MitsubishiCN105ESPHome/pull/2) is a draft with green CI; the user marks it ready or ships it with `/gsd-ship`. Open UAT item: the visual check of the job summary rendering and the HTML report download in the GitHub UI.
- PR 1b plans (01-12..01-15) can start.

## Self-Check: PASSED

- `tests/unit/coverage-baseline.json` carries the CI values and a "CI run" source; `coverage_gate.py ratchet` exits 0 (OK).
- Commit b471b23 is an ancestor of HEAD; `git log origin/main..HEAD -- components/cn105` is empty; `git ls-remote --heads origin tmp/coverage-gate-probe` prints nothing; the probe worktree and local branch are removed; PR 3 is CLOSED.
- Exactly one comment with the sticky marker on PR 2; latest Build run on the PR 1a branch concluded success.

---
*Phase: 01-coverage-safety-net*
*Completed: 2026-10-29*
