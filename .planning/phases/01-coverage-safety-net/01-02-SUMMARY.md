---
phase: 01-coverage-safety-net
plan: 02
subsystem: testing
tags: [googletest, fixture, real-frames, run-harness, gcov, coverage, header-tu, m32, docker]

requires:
  - phase: 01-coverage-safety-net
    provides: "01-01 ESP32 host harness (cn105_harness_variant, Cn105Fixture base, test_platform.h)"
provides:
  - "Complete Cn105Fixture: connect(), run_poll_cycle(), feed/feed_raw/take_tx_frames/run_for, default_responses(), publishes_ (Published snapshots)"
  - "with_checksum / split_frames / hex frame helpers (namespace cn105_test)"
  - "real_frames.h: 23 frames copied verbatim from existing tests, each commented with its source test"
  - "tests/unit/run-harness.sh (--docker, --variant, --filter, --build-dir, --coverage; exit 0/1/2/4/64)"
  - "CN105_COVERAGE option: --coverage on cn105_<v>, cn105_char_<v>, cn105_tests, cn105_legacy_action_tests only"
  - "cn105_headers_<v>: one generated, instrumented, never-linked TU per components/cn105 header (D-10)"
affects: [01-03, 01-05, 01-07, 01-08, 01-09, 01-10, 01-12, 01-13, 01-14, phase-02-simulator]

actuals:
  tokens: 10500
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Coverage is enabled per target (never via global CMAKE_CXX_FLAGS)"
    - "Coverage-only header TUs generated in the build tree with file(GENERATE); compiled for .gcno only"
    - "Fixture drivers (connect, run_poll_cycle) answer requests from real frames; downstream plans add test files only"

key-files:
  created:
    - tests/unit/run-harness.sh
    - tests/unit/characterization/real_frames.h
  modified:
    - tests/unit/characterization/fixture.h
    - tests/unit/characterization/fixture.cpp
    - tests/unit/characterization/test_harness_smoke.cpp
    - tests/unit/CMakeLists.txt
    - tests/unit/harness/CMakeLists.txt
    - tests/unit/characterization/CMakeLists.txt

key-decisions:
  - "Header TUs are generated with file(GENERATE) instead of file(CONFIGURE): same no-rewrite-when-unchanged behaviour, and it keeps the project's cmake_minimum_required 3.14 (file(CONFIGURE) needs 3.18)"
  - "run-harness.sh runs the characterization binary directly with --gtest_filter (not ctest), so the filter uses gtest names (HarnessSmoke.*), and one process runs the whole selection"
  - "Lossnay write frames of test_lossnay.cpp are not copied: they are zero-filled templates completed by encode_lossnay_control(), not captures"

patterns-established:
  - "Characterization rule: pin observed values; a contradiction with research goes in a comment and in the SUMMARY"

requirements-completed: [COV-01, COV-04]

duration: ~25min
completed: 2026-10-29
status: complete
plan_head_before: 5f0cd9b7eb8c218e660dfd7d4e7fa6339f500d0a
plan_head_after: 45f13037316fb9c9c9767b2d329a05d248f15574
commits: 2
---

# Phase 1 Plan 02: Fixture, real frames, run-harness.sh and coverage instrumentation Summary

**The real CN105Climate is now driven end to end by a reusable fixture (handshake on the real FC 7A reply, poll cycle answered by real frames, ClimateCall to 0x41 SET frame, every HA publish recorded with its time), runnable through one command, with CN105_COVERAGE instrumenting every cn105 .cpp and compiling every cn105 .h into its own coverage-only TU.**

## Performance

- **Duration:** ~25 min (start time not captured; estimated)
- **Completed:** 2026-10-29
- **Tasks:** 2
- **Files:** 8 changed, 570 insertions, 4 deletions

## Accomplishments

- `Cn105Fixture` gained `connect()`, `run_poll_cycle()`, `default_responses()`, `feed`, `feed_raw`, `take_tx_frames`, `run_for` and `publishes_` (registered via `add_on_state_callback` before `before_setup()`); free helpers `with_checksum`, `split_frames` (FrameParser based), `hex`. Plan 01-01 members unchanged.
- `real_frames.h`: connect reply, 5 settings, room temp, status, 3 power, error info, 7 SET frames, 6 Lossnay 0x34 responses, all verbatim and commented with their source test.
- `HarnessSmoke` grew from 1 to 6 tests (handshake, request order, request checksums, decoded publish, SET frame); 15 `Harness*` tests pass in Docker.
- `run-harness.sh`: native -m32 probe (exit 2 otherwise), `--docker` re-exec through `docker/run.sh`, empty filter exits 4.
- Coverage verified in `tests/unit/build-cov` (esp32): 17 `.cpp.gcno` for 17 cn105 `.cpp`; 37 `.h.cpp.gcno` for 37 cn105 `.h`; zero `.gcno` in `esphome_esp32`, `harness_platform_esp32` and GoogleTest. `gcov` on a header TU gives 0% line records for never-called inline code (for example `cn105_protocol.h` 73 lines, `frame_parser.h` 42 lines).

## Task Commits

1. **Task 1 (tracer): full fixture, real frames, run-harness.sh, smoke tests** - `79ffd8c` (feat)
2. **Task 2: CN105_COVERAGE and coverage-only header TUs** - `45f1303` (feat)

Tracer gate (automated-only verify): Task 1 verify re-run end to end (`Harness*` 15/15, filter miss exit 4, `components/cn105` unchanged) before expanding to Task 2.

## Observations pinned (characterization rule)

All research values held; nothing contradicted. Observed, now asserted in `test_harness_smoke.cpp`:

- First cycle requests 0x02, 0x03, 0x06, 0x09, 0x04 with request checksums 7B, 7A, 77, 74, 79.
- Poll cycle with `default_responses()`: 4 publishes (the first with `current_temperature` NaN), final state HEAT, 21.5 target, 23.0 current, action IDLE, fan AUTO, swing OFF.
- `make_call().set_mode(HEAT).set_target_temperature(23)` gives exactly one SET frame `FC 41 01 30 10 01 07 00 01 01 00 00 00 00 00 00 00 00 AE 00 C6` and 2 publishes.

## Decisions Made

See `key-decisions`. No architectural decision outside the plan.

## Deviations from Plan

### Auto-fixed / adjusted

**1. [Rule 3 - Blocking compatibility] file(GENERATE) instead of file(CONFIGURE)**
- **Found during:** Task 2
- **Issue:** the plan names `file(CONFIGURE)`, which needs CMake 3.18; `tests/unit/CMakeLists.txt` declares `cmake_minimum_required(VERSION 3.14)`.
- **Fix:** `file(GENERATE OUTPUT ... CONTENT ...)`: also rewrites only when content changed; generated TUs land in the build tree at `harness/header_tu_<v>/<header>.cpp`, identical content.
- **Files modified:** `tests/unit/harness/CMakeLists.txt`
- **Commit:** `45f1303`

**2. [Interface scope] Lossnay captured write frames not added to real_frames.h**
- **Issue:** the interfaces block mentions "the captured write frames" of test_lossnay.cpp. In that test the write frames are zero-filled arrays completed by `encode_lossnay_control()`; they are derived output, not captures, and copying them would invent frames.
- **Fix:** only the six real 0x34 responses were copied; a comment in `real_frames.h` explains why. Plan 01-09/01-13 can derive the write frames from the encoder or add real captures later.

**Total deviations:** 2, none touching `components/cn105`.

## Issues Encountered

None blocking. `-DCN105_COVERAGE=OFF` passed by `run-harness.sh` printed a CMake "unused variable" warning in Task 1 (before the option existed); it is gone after Task 2.

## Verification Results

- `tests/unit/run-harness.sh --docker --variant esp32 --filter 'Harness*'`: 15/15, `[run-harness] OK: esp32`.
- `... --filter 'NoSuchSuite.*'`: exit 4.
- `... --coverage --variant esp32 --filter 'HarnessSmoke.*'`: 6/6; counts above.
- 64-bit pure build: 287/287, also with `-DCN105_COVERAGE=ON` (passes, `.gcno` for `cn105_tests`).
- `git diff --quiet origin/main -- components/cn105`: clean; `git status --porcelain components/cn105`: empty (D-03).

## User Setup Required

None (Docker needed locally for `--docker`).

## Known Stubs

None.

## Threat Flags

None. T-01-33 (header TU count and no esphome/platform `.gcno`), T-01-34 (verbatim frames with source comments) and T-01-35 (no production edit) mitigated as verified above.

## Next Phase Readiness

- Plan 01-03 adds the esp8266 variant: append `esp8266` to `HARNESS_VARIANTS` in `run-harness.sh` and one `cn105_harness_variant(esp8266)` call; coverage and header TUs come with the function.
- Plan 01-05 reads `tests/unit/build-cov` (`.gcno`/`.gcda` of `cn105_<v>`, `cn105_char_<v>`, `cn105_headers_<v>`).

## Self-Check: PASSED

- Files exist: run-harness.sh, real_frames.h, fixture.h/.cpp, test_harness_smoke.cpp, three CMakeLists.
- Commits `79ffd8c` and `45f1303` are ancestors of HEAD.
