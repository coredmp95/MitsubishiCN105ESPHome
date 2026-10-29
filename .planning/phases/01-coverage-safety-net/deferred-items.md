# Deferred items — Phase 01 (coverage safety net)

Out-of-scope discoveries logged by executors. Not fixed by the plan that found them.

## 01-14 (found while running `tests/unit/coverage.sh --docker`)

- **`RemoteTemperatureEntities.Characterize_SameValueDebounced` is order-dependent on the ESP32 variant** (test from plan 01-12,
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
