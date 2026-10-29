---
phase: 01-coverage-safety-net
plan: 01
subsystem: testing
tags: [googletest, esphome-2026.10.0, host-harness, m32, esp32-shim, test-clock, docker]

requires: []
provides:
  - "Unmodified components/cn105/*.cpp compiled and run under GoogleTest against the real ESPHome 2026.10.0 core, USE_ESP32 variant, -m32 (D-01, D-02, D-04, D-20)"
  - "cn105_harness_variant(VARIANT) CMake function (harness_flags_<v>, esphome_<v>, harness_platform_<v>, cn105_<v>) ready for the ESP8266 variant of plan 01-03"
  - "cn105_test platform control API: TestClock (wraps at 2^32), TestLog, TestPrefs, TestWifi, IdfRecorder, reset_platform()"
  - "Fixture base: FakeUart, Cn105Options, ClockFixture, Cn105Fixture (options, before_setup, hp, step, take_tx) - the Phase 2 simulator seam"
  - "tests/unit/docker/run.sh: ubuntu:24.04 + g++-multilib dev image so the -m32 harness builds on a machine without multilib"
affects: [01-02, 01-03, 01-05, 01-07, 01-08, 01-09, 01-10, 01-12, 01-13, 01-14, phase-02-simulator]

actuals:
  tokens: 16600
  tasks: 3
  commits: 4

# Tech tracking
tech-stack:
  added: ["ESPHome 2026.10.0 sources via FetchContent (commit-checked 704551a6)", "Docker dev image cn105-unit-env:ubuntu24.04-<hash>"]
  patterns:
    - "Overlay + force-include (-include esphome/core/defines.h) instead of patching ESPHome"
    - "One definition of each time symbol per variant, all on one TestClock; no ESPHome platform .cpp compiled"
    - "-fno-access-control on test TUs only; cn105 objects built from an OBJECT library without it"
    - "Variant list in a GLOBAL property so the characterization CMake iterates over variants"

key-files:
  created:
    - tests/unit/docker/Dockerfile
    - tests/unit/docker/run.sh
    - tests/unit/harness/CMakeLists.txt
    - tests/unit/harness/overlay/esphome.h
    - tests/unit/harness/overlay/esphome/core/defines.h
    - tests/unit/harness/overlay/esphome/core/version.h
    - tests/unit/harness/overlay/esphome/components/wifi/wifi_component.h
    - tests/unit/harness/shim_esp32/ (21 headers)
    - tests/unit/harness/platform/test_platform.h
    - tests/unit/harness/platform/test_platform.cpp
    - tests/unit/harness/platform/stubs_esp32.cpp
    - tests/unit/characterization/CMakeLists.txt
    - tests/unit/characterization/fixture.h
    - tests/unit/characterization/fixture.cpp
    - tests/unit/characterization/test_harness_smoke.cpp
  modified:
    - .gitignore
    - tests/unit/CMakeLists.txt

key-decisions:
  - "Pure-test include dirs (mocks/, components/cn105) moved from directory scope to target scope so the harness does not inherit the shadowing stub headers"
  - "CN105_HARNESS defaults ON only when CMAKE_SIZEOF_VOID_P is 4; the 64-bit command still builds the 287 pure tests only"
  - "WiFiComponent overlay carries a public connected_ flag so TestWifi can toggle it without a second definition"
  - "Preference backends are owned by a static pool in test_platform.cpp, so ESPPreferenceObject raw pointers stay valid across TestPrefs::clear()"

patterns-established:
  - "Harness variant function: adding ESP8266 needs only a shim dir, stubs_esp8266.cpp and one call"
  - "Task-level TDD for harness APIs: query side stubbed in the tracer, RED test commit, GREEN implementation commit"

requirements-completed: [COV-01, COV-04]

coverage:
  - id: D1
    description: "Unmodified components/cn105 compiles, links and runs under GoogleTest against real ESPHome 2026.10.0 (ESP32 variant, -m32)"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "Esp32.HarnessSmoke.ConnectFrameWrittenAfterGraceDelay (tests/unit/characterization/test_harness_smoke.cpp)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Test-owned clock: esphome::millis wraps at 2^32, delay never sleeps, millis_64 follows the same clock"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "Esp32.HarnessClock.* (3 tests)"
        status: pass
      - kind: other
        ref: "nm gate: exactly one 'T esphome::millis()' across the ESP32 variant objects, in stubs_esp32.cpp.o"
        status: pass
    human_judgment: false
  - id: D3
    description: "Platform observability and per-test isolation (TestLog, TestPrefs, TestWifi, IdfRecorder, fresh App)"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "Esp32.HarnessPlatform.* (4 tests), Esp32.HarnessIsolation.* (2 tests)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Existing 64-bit pure-test build unchanged (287 tests) and components/cn105 untouched"
    requirement: COV-01
    verification:
      - kind: other
        ref: "cmake -S tests/unit -B tests/unit/build && ctest: 287/287; git diff --quiet origin/main -- components/cn105"
        status: pass
    human_judgment: false
  - id: D5
    description: "Docker dev image builds and runs the -m32 harness (no host g++-multilib needed)"
    verification:
      - kind: other
        ref: "tests/unit/docker/run.sh 'cmake ... && ctest -R ^Esp32[.]Harness' from a fresh build dir"
        status: pass
    human_judgment: false

duration: ~35min
completed: 2026-10-29
status: complete
plan_head_before: 8d5a36b72b8f975a729913a30cc4a71cdabd9698
plan_head_after: 2046dfa02999d8d1d18824915fd092cb4459f89c
commits: 4
---

# Phase 1 Plan 01: ESP32 host harness tracer Summary

**The unmodified components/cn105 now runs under GoogleTest on the real ESPHome 2026.10.0 core (USE_ESP32, -m32, built in a Docker dev image), with a single test-owned clock behind esphome::millis(), log/prefs/Wi-Fi/IDF-call observability and a reusable Cn105Fixture; the real CN105Climate writes `FC 5A 01 30 02 CA 01 A8` exactly at t=10000 ms.**

## Performance

- **Duration:** ~35 min (start time was not captured at plan start; estimated from session activity)
- **Completed:** 2026-10-29T20:22Z
- **Tasks:** 3 (Task 3 as RED + GREEN commits)
- **Files:** 37 files changed, 1249 insertions, 4 deletions (25 of them are overlay/shim headers)

## Accomplishments

- Dev image + `tests/unit/docker/run.sh` (repo mounted at the same absolute path, git common dir mounted for worktrees) so the `-m32` build runs without host `g++-multilib`.
- `tests/unit/harness/CMakeLists.txt`: FetchContent of ESPHome 2026.10.0 with a `FATAL_ERROR` commit check (704551a6...), overlay `defines.h` / `version.h` / `esphome.h` / `wifi_component.h`, 21-header ESP32 shim, `esphome_esp32` built from 30 ESPHome sources with no platform HAL `.cpp` (nm gate: zero `esphome::millis()` in it).
- Clock ownership per D-05: `test_platform.cpp` owns the 64-bit `TestClock`; `stubs_esp32.cpp` holds the only `esphome::millis()` plus `xTaskGetTickCount`, `esp_timer_get_time` (feeds the inline `micros()`/`millis_64()` and the scheduler via `USE_NATIVE_64BIT_TIME`) and `vTaskDelay` (feeds the inline `delay()`).
- Fixture base (`FakeUart`, `ClockFixture`, `Cn105Fixture`) with zero-filled `CN105Climate` storage (BUG-08), `App` re-created per test, traits mirroring `climate.py`.
- 10 harness tests pass inside Docker (`HarnessSmoke` 1, `HarnessClock` 3, `HarnessPlatform` 4, `HarnessIsolation` 2), also when run in one process.

## Task Commits

1. **Task 1: ESPHome platform surface (dev image, overlay, ESP32 shim, esphome_esp32)** - `7176f1a` (feat)
2. **Task 2 (tracer): real CN105Climate writes CONNECT after the grace delay** - `578fc37` (feat)
3. **Task 3 RED: clock and platform tests** - `72eb7ff` (test) - 4 platform tests fail against the Task 2 query-side stubs
4. **Task 3 GREEN: platform control API** - `2046dfa` (feat)

**Plan metadata:** committed with this SUMMARY (docs: complete plan).

Tracer gate (auto/end-of-phase, automated-only verify): Task 2 verify re-run end to end, passed - tracer verified, expanded to Task 3.

## Files Created/Modified

See `key-files` above. Notable: `tests/unit/harness/platform/test_platform.h` is the binding control-API contract for plans 01-02, 01-03 and Phase 2.

## Decisions Made

- Moved the pure tests' `mocks/` and component include dirs to `target_include_directories` (plan step; avoids shadowing the real `esphome/core/version.h` and `climate_mode.h`).
- `WiFiComponent` in the overlay got a public `connected_` flag instead of an out-of-line `is_connected()` (observable behaviour identical, no second definition needed).
- Task 2 left the query side of `TestLog` / `TestPrefs` / `TestWifi` / `IdfRecorder` as documented stubs so Task 3 has a genuine RED; Task 3 implemented them.

## Deviations from Plan

None - plan executed exactly as written. (The overlay `is_connected()` being inline with a flag is an implementation detail inside the plan's "implementation details are free".)

**Total deviations:** 0.

## Issues Encountered

- None blocking. The `ctest` per-test-process model means `HarnessIsolation.Pollute` / `StateIsResetInSetUp` only exercise cross-test isolation when the binary is run directly; verified once with `cn105_char_esp32` run in a single process (10/10 pass).
- Plan start time was not recorded, so `duration` is an estimate.

## Verification Results

- `tests/unit/docker/run.sh` fresh `build-m32` configure + build + `ctest -R '^Esp32[.]Harness'`: 10/10 pass.
- nm gate: exactly one `T esphome::millis()` across the ESP32 variant objects, in `stubs_esp32.cpp.o`; `esphome_esp32` objects define none.
- 64-bit pure-test build: 287/287 pass (harness OFF by default on 64-bit).
- `git diff --quiet origin/main -- components/cn105`: clean (D-03, D-21).
- Acceptance greps (GIT_TAG, commit, ESPHOME_LOG_LEVEL_VERY_VERBOSE, USE_NATIVE_64BIT_TIME, VERSION_CODE, portTICK_PERIOD_MS, 21 shim / 4 overlay headers, CN105_HARNESS, .gitignore, fno-access-control, add_subdirectory(characterization), esp_timer_get_time/TestClock, 5 control structs, 9 harness TEST_F): all pass.

## User Setup Required

None - no external service configuration required. (Docker must be available locally to build the `-m32` harness.)

## Known Stubs

None. (The Task 2 query-side stubs were replaced in Task 3.)

## Threat Flags

None - test-side files only; no new network endpoint or trust boundary beyond the plan's threat register (T-01-01 commit check implemented, T-01-02 verified by `git diff`).

## Next Phase Readiness

- Ready for 01-02 (complete fixture, `run-harness.sh`, coverage instrumentation) and 01-03 (ESP8266 variant: add `shim_esp8266/`, `stubs_esp8266.cpp`, the ESP8266 preferences classes next to the ESP32 ones in `test_platform.cpp`, and one `cn105_harness_variant(esp8266)` call).
- Local note: this checkout's tests/unit/build-m32 is gitignored; CI (plan 01-06) installs g++-multilib natively.

## Self-Check: PASSED

---
*Phase: 01-coverage-safety-net*
*Completed: 2026-10-29*
