---
phase: 01-coverage-safety-net
plan: 05
subsystem: testing
tags: [bash, gcovr, diff-cover, gcov, cobertura, docker, coverage, ratchet]

requires:
  - phase: 01-coverage-safety-net
    provides: "-m32 harness + characterization targets (01-01..01-03), coverage_gate.py CLI (01-04)"
provides:
  - "tests/unit/coverage.sh: the single local/CI coverage entry point (--docker, --ci, --gate, --check-scope, --build-dir, --out, --help)"
  - "tests/unit/requirements-coverage.txt + .lock.txt: gcovr 8.6 / diff-cover 10.6.0 and the full resolved set, wheels only"
  - "tests/unit/coverage-baseline.json: provisional baseline for the ratchet"
  - "tests/unit/out/{summary.json,coverage.xml,html/,report.md,...}: line + branch reports of components/cn105 with every .cpp and .h"
affects: [01-06, 01-11, 01-15]

actuals:
  tokens: 5000
  tasks: 3
  commits: 2

tech-stack:
  added: ["gcovr 8.6 (virtualenv, pinned)", "diff-cover 10.6.0 (virtualenv, pinned)"]
  patterns:
    - "One gcovr tracefile per object set (esp32 / esp8266 / pure), merged with -a; header-TU .gcno read as zero counts"
    - "Reports are always written before a failing exit status is returned (1 test, 3 scope, 5 gate)"
    - "Native -m32 probe runs before any tool install, so a host without multilib fails fast with exit 2 and a --docker hint"

key-files:
  created:
    - tests/unit/coverage.sh
    - tests/unit/requirements-coverage.txt
    - tests/unit/requirements-coverage.lock.txt
    - tests/unit/coverage-baseline.json
  modified:
    - tests/unit/tools/coverage_gate.py
    - tests/unit/tools/test_coverage_gate.py

key-decisions:
  - "coverage_gate.py complete now also reports a manifest-listed .cpp that gcov emitted no record for as 0/0 (info_request.cpp is include + comment only); the manifest (compiled-headers.txt) lists header TUs and the .cpp objects present in both variants"
  - "Lock file records diff-cover with a hyphen (pip freeze prints diff_cover); both spellings install the same distribution"
  - "A build failure exits 1 immediately (no object data to report); a test failure still writes every report, then exits 1"
  - "Exit precedence when several conditions apply: test failure (1), scope (3), gate (5)"

patterns-established:
  - "--ci means: no virtualenv bootstrap and no Docker re-exec; the CI job supplies gcovr/diff-cover on PATH"

requirements-completed: [COV-01, COV-02, COV-03]

coverage:
  - id: D1
    description: "coverage.sh --docker builds the -m32 coverage build, runs all 490 tests and writes summary.json, coverage.xml, html/index.html, report.md with line and branch figures"
    requirement: COV-01
    verification:
      - kind: command
        ref: "tests/unit/coverage.sh --docker (exit 0, 490/490 tests passed)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Every components/cn105 .cpp and .h (54 files) is in summary.json and coverage.xml; header-TU probe line of cn105_protocol.h is coverable in tf_esp32.json; declaration-only headers listed 0/0"
    requirement: COV-01
    verification:
      - kind: command
        ref: "coverage_gate.py scope + glob-vs-summary/coverage.xml check + header-TU probe"
        status: pass
    human_judgment: false
  - id: D3
    description: "Ratchet against the provisional baseline reports RATCHET; --gate maps FAIL to exit 5 (proven with a temporary 99.0 baseline); --check-scope, --help, unknown flag (2), no -m32 host (2)"
    requirement: COV-02
    verification:
      - kind: command
        ref: "tests/unit/coverage.sh --gate / --check-scope / --help / --bogus on host and in Docker"
        status: pass
    human_judgment: false
  - id: D4
    description: "coverage_gate.py diff parses real diff-cover 10.6.0 JSON: NO_LINES on this branch, FAIL (0/10) on a range that touched uncovered lines"
    requirement: COV-03
    verification:
      - kind: command
        ref: "diff-cover tests/unit/out/coverage.xml --compare-branch=origin/main --format json + coverage_gate.py diff"
        status: pass
    human_judgment: false

duration: 7min
completed: 2026-10-29
status: complete
plan_head_before: 25cde749f0b4df5d2db2db2a89707bd46bdb7481
plan_head_after: c0df1ed77c615397270df0bcd3726b7f73f8eaf8
commits: 2
---

# Phase 1 Plan 05: coverage.sh Summary

**One command (`tests/unit/coverage.sh --docker`) builds the -m32 coverage build, runs all 490 tests (pure, ESP32 and ESP8266 characterization), and writes merged line + branch reports of every `components/cn105` .cpp and .h; first measured baseline is 67.05% lines (2088/3114) and 60.08% branches (1284/2137), ratchet verdict RATCHET against the provisional baseline.**

## Performance

- **Duration:** ~7 min for Tasks 2-3 (the Task 1 human gate wait is not counted); start 2026-10-29T21:23Z, end 21:30Z
- **Tasks:** 3 (Task 1 was the package-legitimacy gate, answered "approved" by the human)
- **Files:** 4 created, 2 modified (`coverage_gate.py` and its test; no `components/cn105` change)

## Accomplishments

- `coverage.sh`: Docker re-exec, virtualenv bootstrap from the lock (wheels only, `--require-virtualenv`), gcovr 8.6 version check, `-m32` probe, build, stale `.gcda` cleanup, ctest, three tracefiles merged with `-a`, `toolchain.json`, `compiled-headers.txt`, `complete` / `scope` / `ratchet`, assembled `report.md`.
- Tool install proven twice: first from `requirements-coverage.txt`, then (venv deleted) from the generated lock; `pip freeze` equals the lock.
- `--gate` exit 5 proven by temporarily setting the baseline line value to 99.0 (restored; `git diff` on the baseline is empty); without `--gate` the same run exits 0.
- Real-output integration of `coverage_gate.py diff` with diff-cover 10.6.0: top-level keys `diff_name`, `num_changed_lines`, `report_name`, `src_stats`, `total_num_lines`, `total_num_violations`, `total_percent_covered`; per-file keys `percent_covered`, `violation_lines`, `covered_lines`, `violations`. The 01-04 parser needed no key change.

## Task Commits

1. **Task 1: package legitimacy gate** - no commit (human "approved"; installed exactly gcovr 8.6 and diff-cover 10.6.0)
2. **Task 2 (tracer): coverage.sh end to end in Docker** - `60b3889`
3. **Task 3: CI and developer modes** - `c0df1ed`

Tracer gate (auto mode, automated-only verify): the Task 2 verify chain was re-run end to end after the commit and passed - tracer verified, expanded to Task 3.

## Files Created/Modified

- `tests/unit/coverage.sh` - the entry point; `--help` prints the flag and exit-code table.
- `tests/unit/requirements-coverage.txt` / `.lock.txt` - top-level pins; full resolved set (chardet 7.7.0, colorlog 6.12.0, diff-cover 10.6.0, gcovr 8.6, Jinja2 3.1.6, lxml 6.1.3, MarkupSafe 3.0.4, pluggy 1.7.0, Pygments 2.21.0; Python 3.12.3). pip resolved no unexpected top-level package.
- `tests/unit/coverage-baseline.json` - provisional (`provisional: true`, 0.0 / 0.0) until plan 01-11.
- `tests/unit/tools/coverage_gate.py`, `test_coverage_gate.py` - see deviation 1 (70 tests, OK).

## Decisions Made

See `key-decisions`. Observed numbers for plan 01-11 / 01-15: `Lines 2088/3114 (67.1%)`, `Functions 324/488 (66.4%)`, `Branches 1284/2137 (60.1%)`, 54 files in the report. No `--merge-mode-functions` flag was needed (no function merge conflict).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Include-only `.cpp` made the scope check fail (exit 3)**
- **Found during:** Task 2, first Docker run
- **Issue:** `components/cn105/info_request.cpp` contains only two includes and a comment, so gcov emits no record for it. The plan assumed every `.cpp` has executable lines (its objects are always compiled) and that `complete` adds only headers, so scope reported ".cpp missing" and the run exited 3.
- **Fix:** `coverage_gate.py complete` now also adds a manifest-listed `.cpp` as 0/0 (`no_executable_lines`), under a separate "Sources with no executable lines (0/0)" heading; `coverage.sh` lists in the manifest every `.cpp` whose object `.gcno` exists in both variants. Unlisted files are never added. New unit test `test_complete_adds_manifest_listed_empty_cpp_as_zero_zero`; the old "never adds" test was renamed to say unlisted. No shipped source touched, nothing excluded from the denominator.
- **Files modified:** `tests/unit/coverage.sh`, `tests/unit/tools/coverage_gate.py`, `tests/unit/tools/test_coverage_gate.py`
- **Commit:** `60b3889`

**2. [Plan acceptance wording] "Every no_executable_lines entry is a header" cannot hold**
- Consequence of deviation 1: the 13 entries are 12 headers plus `info_request.cpp`. Verified instead that each is declaration-only or include-only (below). The plan's header-count check (`compiled-headers.txt` has one line per header, 37) holds.

**3. [Rule 1 - minor] Lock file spelling**
- `pip freeze` prints `diff_cover==10.6.0`; the generation step rewrites it to `diff-cover==10.6.0` so the plan's acceptance grep and the requirements file agree. Re-install from the lock verified.

**4. [Minor] Build failure exits immediately**
- The plan says reports are still written on "any build or test failure". With a failed build there are no objects to report, so only test failures still produce reports; a build failure exits 1 at once.

**Total deviations:** 4 (1 blocking fix with a test, 3 minor). **Impact:** none on scope; the report denominator is unchanged and complete.

## Files with no executable lines (0/0)

- `Globals.h` - log-tag constants only; `compressor_frequency_sensor.h`, `input_power_sensor.h`, `kwh_sensor.h`, `outside_air_temperature_sensor.h`, `runtime_hours_sensor.h`, `target_humidity_sensor.h` - empty sensor subclasses with a defaulted constructor.
- `cycle_management.h`, `request_scheduler.h`, `hardware_setting_select.h`, `heatpumpFunctions.h`, `uptime_connection_sensor.h` - declarations only (definitions live in the .cpp files).
- `info_request.cpp` - two `#include` lines and a comment.

## Issues Encountered

- Host has no `-m32` libraries and no gcovr, so the native path was verified only for exit 2 and the `--docker` hint; all coverage runs were in the Docker image.
- The scratchpad directory is not mounted in the container: diff-cover scratch outputs must be written under `tests/unit/out/` (ignored).

## Known Stubs

None.

## Threat Flags

None. Mitigations of T-01-06 (no exclusions, scope exit 3, header-TU probe `OK`), T-01-07 (lock + wheels only + virtualenv) and T-01-SC (human gate before install) are in place; `grep GCOVR_EXCL components/cn105` finds nothing and `git diff origin/main -- components/cn105` is empty.

## Verification Results

- `tests/unit/coverage.sh --docker`: exit 0, 490/490 tests pass; `coverage.sh --check-scope`: exit 0; `--help` prints all flags and exits 0/1/2/3/5; `--bogus`: 2; native host without `-m32`: 2 with `--docker` hint.
- 54 `.cpp`/`.h` files all present in `summary.json` and `coverage.xml`; header-TU probe (`return data11 == 0x80;` of `cn105_protocol.h`) is a coverable line in `tf_esp32.json` (JSON keys `files[].file`, `lines[].line_number` matched the plan's probe).
- `report.md` contains line and branch figures and RATCHET; `python3 -m unittest discover -s tests/unit/tools`: 70 tests OK.
- diff-cover to `coverage_gate.py diff`: `NO_LINES` on this branch.

## Next Phase Readiness

Plan 01-06 can call `tests/unit/coverage.sh --ci --gate` from CI; plan 01-11 replaces the provisional baseline with the first ubuntu-24.04 CI run (the local Docker numbers above use the same gcc 13.3.0 / gcovr 8.6 / ubuntu 24.04 toolchain recorded in `toolchain.json`).

## Self-Check: PASSED

- Created files present: coverage.sh, requirements-coverage.txt, requirements-coverage.lock.txt, coverage-baseline.json.
- Commits `60b3889` and `c0df1ed` are ancestors of HEAD.
- Plan-level verification re-run above (coverage.sh --docker, --check-scope, probe, `git diff origin/main -- components/cn105` empty).
