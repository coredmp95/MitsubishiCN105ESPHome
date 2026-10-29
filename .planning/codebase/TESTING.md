---
last_mapped_commit: 5250ba782c261ae98fe54abd2475ae7ea7836475
last_mapped_at: 2026-10-27
---
# Testing Patterns

**Analysis Date:** 2026-10-27

## Test Framework

**Runner:**
- GoogleTest v1.15.2, fetched automatically via CMake `FetchContent` (`tests/unit/CMakeLists.txt`).
- CMake minimum 3.14; C++17 (`CMAKE_CXX_STANDARD 17`).
- Test discovery via `gtest_discover_tests` and CTest.
- Config: `tests/unit/CMakeLists.txt` (no separate gtest config file).

**Assertion Library:**
- GoogleTest `EXPECT_*` / `ASSERT_*` macros (non-fatal vs fatal). Use `ASSERT_TRUE(r.has_value());` before dereferencing an optional-like result (`tests/unit/test_setpoint_persistence.cpp`).

**Python tests:**
- None tracked. `.gitignore` lists `tests/test_hybrid_climate.py`, `tests/test_mqtt_climate.py`, `tests/test_ui_layout.py` as ignored (not present in the repo). No pytest config exists.

**Run Commands:**
```bash
# Configure (from repo root)
cmake -B tests/unit/build -S tests/unit -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build tests/unit/build -j$(nproc)

# Run all tests (as CI does)
cd tests/unit/build && ctest --output-on-failure --no-tests=error

# Run one test binary directly with a filter
./tests/unit/build/cn105_tests --gtest_filter='FrameParser.*'

# Run the legacy-ESPHome variant binary
./tests/unit/build/cn105_legacy_action_tests

# Watch mode: not configured
# Coverage: not configured
```

CI (`.github/workflows/build.yaml`, job `unit-tests`) runs the same configure/build/ctest sequence on `ubuntu-latest`. The `esphome-build` job depends on it (`needs: unit-tests`).

## Test File Organization

**Location:**
- All unit tests are separate from production code, in `tests/unit/`. They are not co-located with `components/cn105/`.
- Production sources under test are compiled directly by CMake from `components/cn105/` (path set via `CN105_SRC_DIR` in `tests/unit/CMakeLists.txt`).

**Naming:**
- Files: `tests/unit/test_<topic>.cpp` (e.g. `test_checksum.cpp`, `test_lookup_tables.cpp`, `test_dual_setpoint_recenter.cpp`, `test_climate_action.cpp`).
- Test suites (the first arg to `TEST`) are PascalCase, optionally ending in `Test`: `CheckSumTest`, `FrameParser`, `SetpointPersistenceTest`, `RealChecksum`, `HeatPumpProfile`, `LookupTablesTest`.
- Test names are descriptive PascalCase or underscore-separated scenario names: `ConnectPacketHasCorrectChecksum`, `OverflowHandling`, `Settings_OFF_HEAT_19_5`, `Settings_Fan2_VaneSwing`, `VersionMismatchDoesNotRestore`.

**Structure:**
```
tests/unit/
├── CMakeLists.txt                     # Builds cn105_tests and cn105_legacy_action_tests
├── test_*.cpp                         # 17 test translation units
└── mocks/
    ├── esphome_stubs.h                # ESP_LOG* no-op macros, minimal helpers
    ├── esphome/core/version.h         # ESPHOME_VERSION_CODE stub
    └── esphome/components/climate/
        └── climate_mode.h             # ClimateMode / ClimateFanMode / ClimateAction enums
```

Approximately 284 `TEST(` cases across 17 files (`tests/unit/*.cpp`). Most files use `TEST(...)`; no `TEST_F` fixtures are used.

## Test Structure

**Suite Organization:**
```cpp
// tests/unit/test_frame_parser.cpp (pattern)
#include <gtest/gtest.h>
#include "frame_parser.h"

using namespace cn105_protocol;

// ════════════════════════════════════════════════════════════════
// Basic lifecycle
// ════════════════════════════════════════════════════════════════

TEST(FrameParser, InitialState) {
    FrameParser parser;
    EXPECT_FALSE(parser.frame_complete());
    EXPECT_EQ(parser.command(), 0);
    EXPECT_EQ(parser.data_length(), 0);
}
```

**Patterns:**
- Each file opens with a `///` header comment naming the file, its purpose, its `Deps:` line, and (for regressions) the bug or issue number. Follow `tests/unit/test_real_frames.cpp` and `tests/unit/test_dual_setpoint_recenter.cpp`.
- Group tests under `// ════` banner comments with a title per scenario group.
- Use an anonymous namespace (`namespace { ... }`) for test-local helpers and simulator structs (`tests/unit/test_dual_setpoint_recenter.cpp` defines `BandSim` and `calcSettingB`).
- Use `TEST(Suite, Name)`; do not use `TEST_F` unless state needs a fixture (none currently).
- Keep one behavior per `TEST`; use `EXPECT_*` to report several independent facts from one scenario.
- Setup pattern: build the object or byte array inline in the test body. Teardown: none needed (RAII objects, no globals).
- Assertion pattern: `EXPECT_EQ(actual, expected)` with the literal value second; use `EXPECT_FLOAT_EQ` for floats; use `EXPECT_FALSE`/`EXPECT_TRUE` for booleans; use `ASSERT_*` when a later line would crash on failure.
- Add a diagnostic message on loops with `<<`: `EXPECT_FALSE(parser.frame_complete()) << "Should not be complete at byte " << i;` (`tests/unit/test_frame_parser.cpp`), and `EXPECT_EQ(...) << "checkSum of CONNECT packet should equal its last byte (0xA8)";` (`tests/unit/test_checksum.cpp`).

**Test naming for frames:** Name real-capture tests after the observed state, e.g. `Settings_ON_HEAT_21_5`, `RoomTemp_23C`. Include the hex frame in a comment or the array itself.

## Mocking

**Framework:** No mocking library (no gmock usage). Dependencies are replaced with hand-written stubs and enums under `tests/unit/mocks/`.

**ESPHome stubs:**
- `tests/unit/mocks/esphome_stubs.h` defines `ESP_LOGD`, `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`, `ESP_LOGV` as no-ops: `#define ESP_LOGD(tag, fmt, ...) ((void)0)`.
- `tests/unit/mocks/esphome/core/version.h` provides `ESPHOME_VERSION_CODE` and `VERSION_CODE(...)`.
- `tests/unit/mocks/esphome/components/climate/climate_mode.h` provides the public ESPHome enums (`ClimateMode`, `ClimateFanMode`, `ClimateAction`) in `namespace esphome::climate`. Its comment states: "Tests exercise production profiles, not copied algorithms."

**Patterns:**
```cpp
// Mocking through include path: production code includes "esphome/..." and the
// mocks directory provides the file. No runtime mock objects.
#include "esphome_stubs.h"
#include "localization.h"
#include "heatpump_profile_protocol.h"
```

**Legacy-version build:**
`cn105_legacy_action_tests` is compiled from `test_climate_action.cpp` with `-DESPHOME_VERSION_CODE=VERSION_CODE(2026,2,0)` (`tests/unit/CMakeLists.txt`). This checks the mapping without the newer `DEFROSTING` enum. Add a similar target when a test must cover a version-gated branch.

**What to Mock:**
- ESPHome logging macros and version macros (always use `esphome_stubs.h`).
- ESPHome enums needed by production headers (`tests/unit/mocks/esphome/components/climate/climate_mode.h`).
- The hardware is never mocked at the UART level. Protocol bytes are fed as literal arrays.

**What NOT to Mock:**
- Production protocol logic. Tests must call the production functions. `HeatPumpProtocol::calculate_temperature_setting`, `HeatPumpProtocol::deadband_setting`, `FrameParser`, `FahrenheitSupport`, and `cn105_protocol::checksum` are the reference points.
- Do not re-implement production algorithms inside a test file. Known violation: `tests/unit/test_checksum.cpp` contains a standalone `checkSum()` copy ("copy from hp_writings.cpp") that is not the production symbol. When adding checksum coverage, call `cn105_protocol::checksum` from `components/cn105/cn105_protocol.h` instead, and migrate the copy.

## Fixtures and Factories

**Test Data:**
Frames are literal `uint8_t` arrays with the full packet including checksum, taken from captured UART logs:
```cpp
// tests/unit/test_real_frames.cpp
TEST(RealChecksum, Settings_OFF_HEAT_19_5) {
    uint8_t pkt[] = {0xFC,0x62,0x01,0x30,0x10, 0x02,0x00,0x00,0x00,0x01,0x1C,0x00,0x00,0x00,0x00,0x03,0xA7,0x00,0x00,0x00,0x00};
    EXPECT_EQ(checksum(pkt, 21), 0x94);
}
```
Protocol constants are shared from `components/cn105/cn105_types.h`: `CONNECT`, `CONNECT_LEN`, `HEADER`, `INFOHEADER`, `RQST_PKT_*`, `RCVD_PKT_*`.

**Location:**
- Inline in each test file. There is no shared fixtures directory.
- Captured frames are documented in the test file header with their source (e.g. "Live debug logs from atom-s3 ESP32").

**Factories:** Helper functions in anonymous namespaces (`calcSettingB`, `BandSim`). Prefer adding helpers to the same test file rather than a new shared header.

## Coverage

**Requirements:** None enforced. No coverage tool, threshold, or CI gate is configured.

**View Coverage:** Not configured. To measure locally, add `--coverage` flags to a CMake build and run `gcov`/`lcov`; this is not in the repo.

## Test Types

**Unit Tests (all of `tests/unit/`):**
- Scope: pure protocol functions, the frame parser, checksum, lookup tables, temperature conversion (`FahrenheitSupport` in `components/cn105/localization.h`), profile logic (`HeatPumpProfile`, `LossnayProfile`), climate action mapping, setpoint persistence, dual-setpoint recentering, and mode preservation.
- Approach: deterministic input bytes and expected values; regression cases named for the issue they fix.
- Files with the most coverage by test-name family: `tests/unit/test_lookup_tables.cpp` (30 `LookupTablesTest` cases), `tests/unit/test_profiles.cpp` (21 `HeatPumpProfile` cases), `tests/unit/test_real_frames.cpp` (18 `RealChecksum` cases).

**Integration Tests:**
- Not present in the unit suite. The ESPHome firmware is validated by compiling example configs in CI, not by running it.

**E2E Tests:**
- Not used. Hardware behavior is verified manually against a real unit (captures are in `captures/` and debug logs are referenced from test headers).

**Compile Tests (CI `esphome-build` job):**
- `.github/workflows/build.yaml` compiles these configs with `esphome compile` on the `ghcr.io/esphome/esphome:stable` container:
  - `examples/root-configs/esp32-test`
  - `examples/root-configs/lossnay-test`
  - `examples/root-configs/hp-debug`
  - `examples/root-configs/hp-sejour`
  - `examples/m5stack-atoms3/atom-m5`
  - `examples/xiao-esp32s3/heatpump-xiaoESP32S3`
  - `examples/modular-hvac/hvac-sample`
- CI generates a random `secrets.yaml` and copies it into the example directories before compiling. Do not commit a real `secrets.yaml`; `.gitignore` excludes it.

## Common Patterns

**Async Testing:** Not applicable. The firmware uses ESPHome's loop and `CUSTOM_MILLIS`; the unit tests drive state with explicit calls and timestamps passed as arguments (see `components/cn105/request_scheduler.cpp`, which takes time from `CUSTOM_MILLIS`, so test code must not depend on wall clock time).

**Error Testing:**
```cpp
// tests/unit/test_setpoint_persistence.cpp
TEST(SetpointPersistenceTest, DisabledDoesNotRestore) {
    auto s = HeatPumpProtocol::saved_setpoint_state(CLIMATE_MODE_HEAT_COOL, 20.0f, 24.0f);
    EXPECT_FALSE(HeatPumpProtocol::restored_setpoint_state(/*restoreEnabled=*/false, true, s, true).has_value());
}

TEST(SetpointPersistenceTest, NoSavedStateDoesNotRestore) {
    SetpointState empty{};  // load() failed
    EXPECT_FALSE(HeatPumpProtocol::restored_setpoint_state(true, /*loaded=*/false, empty, true).has_value());
}
```
Name negative cases `<Condition>DoesNot<Effect>` and pass the flag as a named argument comment (`/*restoreEnabled=*/false`).

**Overflow / bounds testing:** Feed oversized or malformed frames and assert the parser resets (`tests/unit/test_frame_parser.cpp`); `OverflowHandling` in `tests/unit/test_checksum.cpp` shows the wrap-around expectation for `uint8_t` sums.

**Regression testing:** When fixing a bug, add a test file or a `TEST` block named for the issue, with the hardware observation in the header comment (pattern: `tests/unit/test_dual_setpoint_recenter.cpp` for issue #673, `tests/unit/test_setpoint_persistence.cpp` for the reboot band loss).

**Adding a new test file:**
1. Create `tests/unit/test_<topic>.cpp` with the `///` header comment and `#include <gtest/gtest.h>`.
2. Add the file to the `add_executable(cn105_tests ...)` list in `tests/unit/CMakeLists.txt`. CMake does not glob; an unlisted file is silently not built.
3. Add any new production `.cpp` it depends on to the same `add_executable` list (for example `heatpump_functions_protocol.cpp`).
4. Add ESPHome stubs under `tests/unit/mocks/` only if the production header includes a new ESPHome path.
5. Build and run with the commands above.

---

*Testing analysis: 2026-10-27*
