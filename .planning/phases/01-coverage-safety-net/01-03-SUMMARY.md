---
phase: 01-coverage-safety-net
plan: 03
subsystem: testing
tags: [googletest, esp8266, esphome-2026.10.0, host-harness, m32, test-clock, millis64, emulate-mutex, coverage, docker]

requires:
  - phase: 01-coverage-safety-net
    provides: "01-01 cn105_harness_variant(), TestClock/test_platform contract, 01-02 fixture, run-harness.sh and CN105_COVERAGE header TUs"
provides:
  - "USE_ESP8266 variant of the harness: the unmodified components/cn105 compiles, links and runs on the real ESPHome 2026.10.0 core, -m32, with the non-ESP32 branches (volatile bool mutex emulation, no low-level UART reinit)"
  - "tests/unit/harness/shim_esp8266/ (pgmspace.h, c_types.h, core_esp8266_features.h, Esp.h, WString.h, coredecls.h), force-included first in the variant"
  - "stubs_esp8266.cpp: the variant's only esphome::millis(), esphome::delay(), ::millis(), ::micros(), all on TestClock; millis_64 stays ESPHome's inline Millis64Impl over esphome::millis()"
  - "ESP8266 preferences backend and F()-format log overload in test_platform.cpp (under USE_ESP8266)"
  - "cn105_char_esp8266 (ctest prefix Esp8266.), HARNESS_VARIANTS=\"esp32 esp8266\", coverage .gcno/header TUs identical to ESP32"
affects: [01-05, 01-07, 01-08, 01-09, 01-10, 01-12, 01-13, 01-14, phase-02-simulator, phase-07-mutex]

actuals:
  tokens: 4000
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Force-includes use SHELL:-include <file> so CMake keeps each pair (a bare repeated -include is de-duplicated)"
    - "Variant differences in tests are static_asserts or #if with a comment stating why they differ; assertions are never weakened for both"

key-files:
  created:
    - tests/unit/harness/shim_esp8266/pgmspace.h
    - tests/unit/harness/shim_esp8266/c_types.h
    - tests/unit/harness/shim_esp8266/core_esp8266_features.h
    - tests/unit/harness/shim_esp8266/Esp.h
    - tests/unit/harness/shim_esp8266/WString.h
    - tests/unit/harness/shim_esp8266/coredecls.h
    - tests/unit/harness/platform/stubs_esp8266.cpp
  modified:
    - tests/unit/harness/CMakeLists.txt
    - tests/unit/harness/platform/test_platform.cpp
    - tests/unit/characterization/CMakeLists.txt
    - tests/unit/characterization/test_harness_smoke.cpp
    - tests/unit/run-harness.sh

key-decisions:
  - "Preferences backend pool is static (like the ESP32 one), so ESPPreferenceObject pointers survive TestPrefs::clear()"
  - "random_uint32() is defined in stubs_esp8266.cpp (ESPHome's core helpers.cpp leaves it to the platform on ESP8266); delay_microseconds_safe is NOT stubbed because core/helpers.cpp already defines it"
  - "static_assert on wantedSettingsMutex (std::mutex vs volatile bool) pins in the build which side of cn105's platform branches each binary compiles"

patterns-established:
  - "Adding a platform variant = shim dir + stubs_<v>.cpp + preferences under #if + one cn105_harness_variant() call + variant name in HARNESS_VARIANTS"

requirements-completed: [COV-01, COV-04]

coverage:
  - id: D1
    description: "Unmodified components/cn105 runs on the ESP8266 variant: CONNECT written after the grace delay on the test clock"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "Esp8266.HarnessSmoke.ConnectFrameWrittenAfterGraceDelay (tests/unit/characterization/test_harness_smoke.cpp)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Exactly one esphome::millis() and one ::millis() in the ESP8266 variant, both in stubs_esp8266.cpp on TestClock; wrap tests pass through ESPHome's own Millis64Impl rollover tracking"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "Esp8266.HarnessClock.MillisWrapsAt32Bits, Esp8266.HarnessClock.Millis64ContinuesAcrossWrap"
        status: pass
      - kind: other
        ref: "nm gate over tests/unit/build-m32/*_esp8266.dir objects: 1 'T esphome::millis()', 1 'T millis', esphome::millis() in stubs_esp8266.cpp.o"
        status: pass
    human_judgment: false
  - id: D3
    description: "Whole Harness* suite green on both variants (ESP32 15, ESP8266 14 tests); the emulated-mutex branch is the one compiled in the ESP8266 binary"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "tests/unit/run-harness.sh --docker --variant all --filter 'Harness*' -> [run-harness] OK: esp32 esp8266; static_assert on wantedSettingsMutex"
        status: pass
    human_judgment: false
  - id: D4
    description: "Coverage instrumentation identical on ESP8266: 17/17 cn105 .cpp and 37/37 header-TU .gcno, none for esphome_esp8266/harness_platform_esp8266"
    requirement: COV-01
    verification:
      - kind: other
        ref: "tests/unit/run-harness.sh --docker --coverage --variant all --filter 'Harness*' plus find/wc gcno counts"
        status: pass
    human_judgment: false
  - id: D5
    description: "components/cn105 unchanged"
    requirement: COV-01
    verification:
      - kind: other
        ref: "git diff --quiet origin/main -- components/cn105"
        status: pass
    human_judgment: false

duration: ~25min
completed: 2026-10-29
status: complete
plan_head_before: 73242a289409e12537fa16d13ca3e61fbbc05c84
plan_head_after: 3fbeb9afe9b473aba378b8360a5f2a6f2a2e7456
commits: 2
---

# Phase 1 Plan 03: ESP8266 harness variant Summary

**The unmodified components/cn105 now also runs under GoogleTest as a 32-bit USE_ESP8266 build on the real ESPHome 2026.10.0 core, with a single test-clock-driven esphome::millis()/::millis() pair and ESPHome's own Millis64Impl rollover tracking, so `run-harness.sh --variant all` pins the std::mutex (ESP32) and the volatile-bool emulateMutex (ESP8266) sides for Phase 7.**

## Performance

- **Duration:** ~25 min (start time not captured; estimated)
- **Completed:** 2026-10-29
- **Tasks:** 2
- **Files:** 12 changed, 218 insertions, 4 deletions

## Accomplishments

- Six ESP8266 shim headers (`pgmspace.h` with `PSTR`, `F()`, `pgm_read_*` and all `*_P` aliases; `c_types.h`; `core_esp8266_features.h`; `Esp.h`; `WString.h`; `coredecls.h`), force-included before `defines.h` through `SHELL:-include pgmspace.h`. `ARDUINO` is not defined.
- `stubs_esp8266.cpp`: clock ownership exactly as plan 01-01 fixed it. `esphome::millis_64()` has no definition (inline `Millis64Impl::compute(millis())` in `esp8266/hal.h`), so the rollover tracking runs on the test clock.
- ESP8266 preferences backend (static pool, shared in-memory store) next to the ESP32 one in `test_platform.cpp`, plus the `__FlashStringHelper*` overload of `esp_log_printf_`.
- `cn105_harness_variant(esp8266)` and `HARNESS_VARIANTS="esp32 esp8266"`: coverage flags and header TUs came with the function (Pattern 6, one `harness_flags_esp8266` target).
- `test_harness_smoke.cpp`: `static_assert` that `wantedSettingsMutex` is `std::mutex` on ESP32 and `volatile bool` on ESP8266; every `#ifdef USE_ESP32` there now carries a comment stating why the variants differ.

## Task Commits

1. **Task 1 (tracer): ESP8266 variant end to end** - `973c712` (feat)
2. **Task 2: whole smoke suite and coverage on both variants** - `3fbeb9a` (test)

Tracer gate (automated-only verify, end-of-phase mode): Task 1 verify re-run end to end (4 tests, nm gate, `components/cn105` diff) before expanding to Task 2.

## Decisions Made

See `key-decisions`. No architectural decision outside the plan.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] CMake de-duplicates repeated `-include` options**
- **Found during:** Task 1 (design of the force-include order)
- **Issue:** two bare `-include <file>` options in one `target_compile_options` would lose the second `-include` to option de-duplication.
- **Fix:** use `"SHELL:-include pgmspace.h"` and `"SHELL:-include esphome/core/defines.h"`; the ESP32 variant keeps the same single force-include semantics.
- **Files modified:** `tests/unit/harness/CMakeLists.txt`
- **Commit:** `973c712`

**2. [Rule 3 - Blocking] Two stubs from the research prototype are not needed or conflict**
- **Found during:** Task 1 link
- **Issue:** `esphome::delay_microseconds_safe` is already defined by `core/helpers.cpp` (duplicate symbol), while `esphome::random_uint32()` is left to the platform on ESP8266 (undefined reference from `random_float` and the scheduler).
- **Fix:** removed the first stub, defined `random_uint32()` (returns 4, like the ESP32 `random_bytes` stub). `random_bytes` is not stubbed.
- **Files modified:** `tests/unit/harness/platform/stubs_esp8266.cpp`
- **Commit:** `973c712`

**3. [Scope addition inside the plan's test files] Mutex-flavour static_assert**
- **Issue:** the plan requires that the ESP8266 binary compiles the non-ESP32 branches but offers no executable check; the smoke tests behave the same on both variants.
- **Fix:** compile-time `static_assert` on `decltype(CN105Climate::wantedSettingsMutex)` per variant in `test_harness_smoke.cpp`.
- **Commit:** `3fbeb9a`

**Total deviations:** 3, none touching `components/cn105`.

## Issues Encountered

- A `libgcov profiling error ... overwriting an existing profile data with a different checksum` line appeared once on the first coverage run after the smoke test file changed (the post-build `gtest_discover_tests` run wrote a `.gcda` that the previous binary's data disagreed with). `run-harness.sh` deletes all `.gcda` before running; the second run was clean. Harmless.
- Plan start time was not captured; `duration` is an estimate.

## Verification Results

- `tests/unit/run-harness.sh --docker --variant esp8266 --filter 'HarnessSmoke.ConnectFrameWrittenAfterGraceDelay:HarnessClock.*'`: 4 tests selected, `[run-harness] OK: esp8266`.
- nm gate: exactly one `T esphome::millis()` and one `T millis` across the `*_esp8266.dir` objects, `esphome::millis()` in `stubs_esp8266.cpp.o`.
- `tests/unit/run-harness.sh --docker --variant all --filter 'Harness*'`: ESP32 15/15, ESP8266 14/14, `[run-harness] OK: esp32 esp8266`.
- `ctest --test-dir tests/unit/build-m32 -N | grep -c 'Esp8266\.'`: 14 (floor 13: 6 HarnessSmoke + 3 HarnessClock + 3 HarnessPlatform + 2 HarnessIsolation; only `HarnessPlatform.IdfCallsRecorded` is ESP32-only, as in the plan).
- Coverage (`--coverage --variant all`): 17 `.cpp.gcno` in `cn105_esp8266.dir` for 17 `components/cn105/*.cpp`; 37 header-TU `.gcno` for 37 `*.h`; 0 `.gcno` in `esphome_esp8266` and `harness_platform_esp8266`.
- Acceptance greps: 6 shim headers; `USE_ESP8266` in `harness/CMakeLists.txt` >= 1; `HARNESS_VARIANTS=` line names esp8266; `TestClock` in `stubs_esp8266.cpp` (10); every `#ifdef USE_ESP32` in `test_harness_smoke.cpp` has a why-comment.
- `git diff --quiet origin/main -- components/cn105`: clean (D-03).

## User Setup Required

None (Docker needed locally for `--docker`).

## Known Stubs

None. The stubs added are link-level platform replacements by design (D-05), not data stubs.

## Threat Flags

None. T-01-36 (no production edit) and T-01-37 (one `harness_flags_esp8266` target, nm single-definition check) mitigated as verified above.

## Next Phase Readiness

- Plans 01-05 onward can run every characterization test on both variants: add `test_*.cpp` files only; the `Esp8266.` ctest prefix and `cn105_char_esp8266` binary appear automatically.
- Variant-specific expectations (mutex path, IDF recorder) must be written with `#if defined(USE_ESP32)` plus a why-comment, as in `test_harness_smoke.cpp`.

## Self-Check: PASSED

- Files exist: six `shim_esp8266/*.h`, `stubs_esp8266.cpp`, the modified CMake/script/test files.
- Commits `973c712` and `3fbeb9a` are ancestors of HEAD.
