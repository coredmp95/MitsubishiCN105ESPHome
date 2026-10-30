# Deferred items — Phase 01 (coverage safety net)

Out-of-scope discoveries logged by executors. Not fixed by the plan that found them.

## 01-14 (found while running `tests/unit/coverage.sh --docker`)

- **RESOLVED** by the commit `fix(01): make harness randomness deterministic per test (wave 8 post-merge gate)` (see the resolution below).
  **`RemoteTemperatureEntities.Characterize_SameValueDebounced` is order-dependent on the ESP32 variant** (test from plan 01-12,
  `tests/unit/characterization/test_remote_temperature.cpp`, line 160).
  - Passes when the whole binary runs in one process (`tests/unit/run-harness.sh`, 168/168), fails when run alone
    (`run-harness.sh --variant esp32 --filter RemoteTemperatureEntities.Characterize_SameValueDebounced`) and under `ctest`
    (each test in its own process): `remote_temp_debounce_skip_count_` is 2 instead of 1.
  - Probable cause: the ESP32 platform stub `random_bytes()` (`tests/unit/harness/platform/stubs_esp32.cpp`) is a process-global
    sequence, so the random start offset ESPHome gives the keep-alive interval depends on how many tests ran before. A keep-alive
    tick then also lands inside the debounce window and is counted.
  - Effect: `tests/unit/coverage.sh` reports "tests failed (ctest exit 8)" although coverage is produced; the CI ctest step would
    fail the same way. Fix belongs to plan 01-12's file (reset the random sequence in `reset_platform()`, or assert `>= 1` with the
    keep-alive explicitly disabled in that test). Plan 01-14 touches neither file.
  - Coverage observed in the same run (baseline file untouched by this plan): line 84.87% (2648/3120), branch 73.84% (1578/2137).
  - **Resolution (post-merge gate of wave 8).** The probable cause above was right in effect and wrong in detail: the
    `random_bytes()` stub is a constant (every byte 4); the sequence is ESPHome's own `random_uint32()` (`core/helpers.cpp`,
    every platform but ESP8266), a splitmix32 over a file-static state seeded once per process and advanced by every call.
    The scheduler draws from it for the start offset of each `set_interval()` (`(r * min(interval / 2, 5000)) >> 32`), so the
    first keep-alive tick of the test landed 1630 ms after the value in a fresh process (after the packet at 1060 ms, before the
    next cycle end: counted as debounce skip #1) and somewhere else after N earlier draws. Confirmed by sweeping N pre-draws
    in the test alone: the measured tick matched a splitmix32 replica for every N (N=0 1630 ms, N=2 2410, N=3 1470, N=9 1670),
    and the skip count was 1 exactly when the tick fell between the send and the next cycle end. Before the fix 27 of 40
    `--gtest_shuffle` seeds failed on ESP32 (only this test), 0 of 40 on ESP8266 (`random_uint32()` stub is a constant there).
  - Fix: `cn105_test::TestRandom` (`harness/platform/test_platform.{h,cpp}`) is the one source of `esphome::random_uint32()` in
    both variants, a constant (default 4, so every interval starts with an offset of 0 ms) that tests can pin with `set()` and
    that `reset_platform()` resets. The ESP32 variant compiles `core/helpers.cpp` through a generated wrapper that renames the
    upstream function (`harness/CMakeLists.txt`; the fetched file is not patched) and `stubs_esp32.cpp` defines the real one.
    The test pins the offset explicitly (0 ms, its original expectation unchanged) and a new test,
    `Characterize_KeepAliveTickInsideDebounceWindowIsCounted`, pins the case that used to leak in randomly (a tick inside the
    debounce window is swallowed and counted).
  - Verified: one process 169/169 (ESP32) and 167/167 (ESP8266); `ctest -j8` 623/623 (pure 287 + both variants), also three
    times with `--schedule-random`; 40 shuffled one-process runs per variant, 0 failures; 64-bit pure build 287/287;
    `components/cn105` untouched.
