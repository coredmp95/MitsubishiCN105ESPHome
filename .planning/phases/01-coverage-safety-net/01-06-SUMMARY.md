---
phase: 01-coverage-safety-net
plan: 06
subsystem: testing
tags: [github-actions, ci, coverage, gcovr, diff-cover, ratchet, bash, docs]

requires:
  - phase: 01-coverage-safety-net
    provides: "coverage_gate.py CLI (01-04), tests/unit/coverage.sh --ci and the pinned tool lock (01-05), Docker dev image (01-01)"
provides:
  - ".github/workflows/build.yaml: unit-tests on ubuntu-24.04 with g++-multilib publishing the coverage report; coverage-gate job; esphome-build unchanged"
  - "tests/unit/tools/ci_coverage_gate.sh: self-test, ratchet with previous baseline, diff-cover at 80% with live coverage-exempt label, step summary, sticky PR comment, dry-run mode"
  - "tests/unit/README.md and a README.md section: one local command, ratchet and exemption procedures, characterization conventions, ESPHome bump"
affects: [01-11, 01-15]

actuals:
  tokens: 6000
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Workflow context values reach shell logic only through env:, never inside run text; all gate logic lives in a testable script with a dry-run mode"
    - "Least privilege: workflow-level contents: read, extra scopes only on the one job that comments"
    - "Reports and the sticky comment are written before the job exits non-zero"

key-files:
  created:
    - tests/unit/tools/ci_coverage_gate.sh
    - tests/unit/README.md
  modified:
    - .github/workflows/build.yaml
    - README.md

key-decisions:
  - "download-artifact is pinned at v8 and upload-artifact at v7: those are the current majors of each action (they are versioned independently) and both use the artifact v4+ backend"
  - "The sticky comment is built for every pull request (also forks, also dry-run) but posted only for same-repository PRs; a comment failure is a warning, never a job failure"
  - "Label read failure is treated as not exempt (conservative) with a warning"
  - "On push events the previous baseline is HEAD^1's file, so a baseline lowered on main is caught"

patterns-established:
  - "CN105_GATE_DRY_RUN=1 + CN105_GATE_LABELS: run the whole PR gate locally without the GitHub API"

requirements-completed: [COV-01, COV-02, COV-03]

coverage:
  - id: D1
    description: "unit-tests job runs on ubuntu-24.04 with g++-multilib, keeps the 64-bit steps, runs coverage.sh --ci, appends report.md to the job summary and uploads tests/unit/out as coverage-report"
    requirement: COV-01
    verification:
      - kind: command
        ref: "python3 yaml assertions on build.yaml + tests/unit/docker/run.sh venv install from the lock and coverage.sh --ci (report.md non-empty)"
        status: pass
    human_judgment: false
  - id: D2
    description: "coverage-gate job and ci_coverage_gate.sh: self-test, ratchet, diff coverage at 80% with coverage-exempt label, sticky comment created then updated in place, fork PRs get no comment"
    requirement: COV-02
    verification:
      - kind: command
        ref: "dry-run in Docker (marker printed, exit 0); fake diff-cover FAIL 5/10 -> exit 1 without label, EXEMPT exit 0 with label; fake gh POST then PATCH of the oldest marker comment"
        status: pass
    human_judgment: false
  - id: D3
    description: "Real GitHub behaviour: summary rendering, artifact upload/download, comment update in place, label exemption on a live PR"
    requirement: COV-03
    verification: []
    human_judgment: true
    rationale: "Cannot run GitHub Actions here; verified on the real PR in plan 01-11 (as the plan states)"
  - id: D4
    description: "tests/unit/README.md documents the commands, policy, ratchet, exemption, conventions, header counting and ESPHome bump; README has the Development section and the corrected comparison cell"
    requirement: COV-01
    verification:
      - kind: command
        ref: "grep of heading, coverage.sh --docker, coverage-exempt, Characterize_BUG; 'Full C++ test coverage' count 0"
        status: pass
    human_judgment: false

duration: 12min
completed: 2026-10-29
status: complete
plan_head_before: 71795b071927a19d682d0dffee3de0db2146768e
plan_head_after: f9feb5bac4bafb445247a43a21f4e76937414a0d
commits: 3
---

# Phase 1 Plan 06: CI coverage wiring Summary

**Every CI run now produces and publishes line + branch coverage of `components/cn105` (job summary + `coverage-report` artifact) on a pinned `ubuntu-24.04`, a `coverage-gate` job enforces the ratchet and 80% changed-line coverage with a live `coverage-exempt` label and keeps one sticky PR comment, and the one-command local reproduction and the ratchet/exemption procedures are documented.**

## Performance

- **Duration:** ~12 min (2026-10-29T21:24Z to 21:36Z)
- **Tasks:** 3 (Task 1 was a tracer)
- **Files:** 2 created, 2 modified; no `components/cn105` change

## Accomplishments

- `unit-tests`: `ubuntu-24.04`, existing Configure/Build/Run-tests steps untouched, then toolchain install (g++-multilib + virtualenv from the lock, wheels only), `coverage.sh --ci`, summary, `upload-artifact@v7` (14 days).
- `coverage-gate` (needs `unit-tests`): full-depth checkout, `download-artifact@v8`, same virtualenv install, `ci_coverage_gate.sh` with env-only inputs; job permissions `contents: read`, `pull-requests: write`, `issues: write`. `esphome-build` still `needs: unit-tests` with its 7-config matrix.
- `ci_coverage_gate.sh`: gate self-test (D-14), base-branch baseline via `git show`, ratchet, `diff-cover --format json:` then `coverage_gate.py diff --threshold 80`, label read via `gh api`, step summary, marker comment via `comment-target` (PATCH oldest, else POST), verdict last.
- Docs: `tests/unit/README.md` (new) and the `## Development: tests and coverage` README section; comparison-table cell no longer claims "Full C++ test coverage".

## Task Commits

1. **Task 1 (tracer): unit-tests job publishes the coverage report** - `2402506`
2. **Task 2: coverage-gate job and ci_coverage_gate.sh** - `7cb8a5b`
3. **Task 3: developer documentation** - `f9feb5b`

Tracer gate (auto mode, automated-only verify): YAML assertions plus the Docker run of the exact CI install recipe and `coverage.sh --ci` passed - tracer verified end to end, expanded to Task 2.

## Decisions Made

See `key-decisions`. Artifact action majors differ (v7 upload, v8 download) although the plan said "same major": the actions are versioned independently and these are the latest releases of each (checked with `gh api .../releases/latest`).

## Deviations from Plan

### Auto-fixed Issues

**1. [Minor] Artifact action majors**
- **Found during:** Task 2
- **Issue:** The plan asks for the "same major" for download-artifact as upload-artifact, but upload-artifact is at v7 and download-artifact at v8 (independent versioning).
- **Fix:** upload-artifact@v7 and download-artifact@v8, both first-party and on the artifact v4+ backend. The "first-party actions only" acceptance grep still passes.
- **Files modified:** `.github/workflows/build.yaml`
- **Commit:** `7cb8a5b`

**Total deviations:** 1 minor. **Impact:** none.

## Verification Results

- YAML structure assertions (both tasks): `ok`; `python3 -m unittest discover -s tests/unit/tools`: 70 tests OK.
- Task 1 acceptance greps pass (ubuntu-24.04, g++-multilib, `coverage.sh --ci` present; no codecov/coveralls; only checkout/upload-artifact/download-artifact actions).
- Task 2 acceptance greps pass: no `pull_request_target`, no head-ref/title/body expressions, no `git commit|push`, no `contents: write`, `needs: unit-tests` in exactly `coverage-gate` and `esphome-build`.
- Gate dry-run in the ubuntu:24.04 image (venv from the lock, `CN105_GATE_DRY_RUN=1`): exit 0, comment body contains `<!-- cn105-coverage-report -->`; with `CN105_GATE_LABELS=coverage-exempt` the body still prints the measured diff result (`NO_LINES` on this branch).
- With a fake `diff-cover` (5/10 changed lines): no label gives `FAIL`, exit 1, summary and comment still written; with the label gives `EXEMPT 50.00%`, exit 0. With a fake `gh`: no marker comment gives POST; markers on ids 9 and 12 give PATCH of 9 (oldest); `SAME_REPO=false` gives no comment call; push event with the real previous baseline gives OK.
- `actionlint` is not installed here; the workflow was validated by YAML load and the structural assertions only. Real GitHub behaviour is verified on the PR in plan 01-11.

## Known Stubs

None.

## Threat Flags

None. T-01-08 (scoped permissions asserted), T-01-09 (env-only inputs, greps), T-01-10 (plain `pull_request`, comment only for same-repo), T-01-11 (first-party actions only), T-01-12 (no commit/push) and T-01-SC (install only from the lock, wheels only, job-local virtualenv) are in place.

## Next Phase Readiness

Plan 01-11 opens the PR: it must check the real run (job summary, artifact, comment in place, label exemption), replace the provisional baseline with the first `ubuntu-24.04` CI numbers, and create the `coverage-exempt` label in the repository.

## Self-Check: PASSED

- Created files present: `tests/unit/tools/ci_coverage_gate.sh`, `tests/unit/README.md`.
- Commits `2402506`, `7cb8a5b`, `f9feb5b` are ancestors of HEAD; `git rev-list --count` from the ledger base = 3.
- `git diff origin/main -- components/cn105` empty (no production source touched).
