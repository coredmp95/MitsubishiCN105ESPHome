# Phase 1: Coverage Safety Net - Pattern Map

**Mapped:** 2026-10-27
**Files analyzed:** 27 new/modified
**Analogs found:** 9 / 27 (Phase 1 is mostly new infrastructure; the codebase has one CMake test project, one CI workflow, and a set of pure GoogleTest files. Everything else must follow 01-RESEARCH.md recipes.)

Note: RESEARCH.md was read lines 1-451 only (harness recipe, platform variants, pitfalls, gcovr commands). The rest (CI job layout, gate script details) is not re-derived here. The planner should consult RESEARCH.md lines 452+ for those.

All analog paths below were verified git-tracked (`git ls-files tests/unit`). `.github/workflows/build.yaml` and `components/cn105/*` are tracked source.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `tests/unit/CMakeLists.txt` (modify) | config (build) | batch | itself (lines 1-65) | exact (self-extension) |
| `tests/unit/harness/CMakeLists.txt` | config (build) | batch | `tests/unit/CMakeLists.txt` | role-match |
| `tests/unit/harness/overlay/esphome/core/defines.h`, `version.h`, `esphome.h`, `esphome/components/wifi/wifi_component.h` | config (header overlay) | transform | `tests/unit/mocks/esphome/core/version.h` | partial (same role, but real-core overlay instead of mock) |
| `tests/unit/harness/shim_esp32/**`, `shim_esp8266/**` | utility (platform shim headers) | transform | `tests/unit/mocks/esphome_stubs.h` | partial |
| `tests/unit/harness/platform/test_platform.{h,cpp}` (test clock, log capture, in-memory preferences, Wi-Fi stub; one file pair since the 2026-10-27 plan split) | utility (test clock + platform control) | event-driven | none (see No Analog); seam is `Globals.h:11-12` | none |
| `tests/unit/harness/platform/stubs_esp32.cpp`, `stubs_esp8266.cpp` | utility (link stubs) | request-response | `tests/unit/mocks/esphome_stubs.h` | partial |
| `tests/unit/characterization/fixture.{h,cpp}` | test (fixture + FakeUart) | event-driven | none for `TEST_F`; RESEARCH.md "Fixture skeleton" | none |
| `tests/unit/characterization/test_cycle_management.cpp` | test | request-response | `tests/unit/test_frame_parser.cpp` (style) + `components/cn105/cycle_management.cpp` (subject) | role-match |
| `tests/unit/characterization/test_request_scheduler.cpp` | test | event-driven | `tests/unit/test_frame_parser.cpp` | role-match |
| `tests/unit/characterization/test_hp_writings.cpp` | test | request-response | `tests/unit/test_real_frames.cpp` (frame bytes) | role-match |
| `tests/unit/characterization/test_climate_controls.cpp` | test | request-response | `tests/unit/test_setpoint_persistence.cpp` / `test_mode_preservation.cpp` | role-match |
| `tests/unit/characterization/test_profile_state.cpp` | test | request-response | `tests/unit/test_profiles.cpp`, `test_lossnay.cpp` | role-match |
| PR 1b: further `test_*.cpp` (sensors, selects, switches, hardware settings, remote temp) | test | request-response | same as above | role-match |
| `tests/unit/coverage.sh` | script (entry point) | batch | none; CI step lines `build.yaml:21-28` are the sequence to wrap | partial |
| `tests/unit/coverage-baseline.json` | config | CRUD | none | none |
| `tests/unit/requirements-coverage.txt` | config | - | none | none |
| `tests/unit/tools/coverage_gate.py` + `test_coverage_gate.py` | utility + test | transform | none (Python file header convention from CLAUDE.md `## file — desc / ## Role / ## Deps`) | none |
| `tests/unit/README.md` | doc | - | none | none |
| `.github/workflows/build.yaml` (modify) | config (CI) | batch | itself, `unit-tests` job lines 15-28 | exact (self-extension) |
| `README.md` (modify, short coverage section) | doc | - | itself | - |

## Pattern Assignments

### `tests/unit/CMakeLists.txt` (modify) and `tests/unit/harness/CMakeLists.txt`

**Analog:** `tests/unit/CMakeLists.txt` (existing).

**Header comment convention** (lines 1-3): `## CMakeLists.txt — <description>` / `## Role:` / `## Deps:`. Keep it for new CMake files (write in English).

**FetchContent pattern** (lines 11-20) to copy for the `esphome` declaration (GIT_TAG `2026.10.0`, `GIT_SHALLOW TRUE`; esphome is not a CMake project, so use `FetchContent_Populate` or `FetchContent_MakeAvailable` per RESEARCH.md "verified"):
```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
```

**Source dir variable** (line 23): `set(CN105_SRC_DIR "${CMAKE_SOURCE_DIR}/../../components/cn105")` - reuse in harness (production sources compiled unmodified).

**Test registration pattern** (lines 56-59, 61-65): `enable_testing()`, `include(GoogleTest)`, `gtest_discover_tests(<target>)`; the second binary shows the per-binary variant pattern (`TEST_PREFIX "LegacyESPHome."`, `target_compile_definitions`). Use `TEST_PREFIX` per variant (e.g. `Esp32.`, `Esp8266.`) for the characterization executables.

**Critical constraint (RESEARCH Anti-Patterns):** line 26 `include_directories(${CMAKE_SOURCE_DIR}/mocks)` is directory-scoped and would shadow real `esphome/core/version.h` and `climate_mode.h` in the harness. Either call `add_subdirectory(harness)` BEFORE line 26, or convert lines 26-28 to `target_include_directories` on `cn105_tests` and `cn105_legacy_action_tests`. Also keep `CMAKE_CXX_STANDARD 17` (line 8) for the existing targets only; harness targets use `-std=gnu++20 -m32 --coverage -O0`, with `-fno-access-control` on test TUs only. Do not set global `CMAKE_CXX_FLAGS=--coverage`.

---

### `tests/unit/harness/overlay/esphome/core/version.h` (config header)

**Analog:** `tests/unit/mocks/esphome/core/version.h` (mock default `ESPHOME_VERSION_CODE` 2026.3.0). The overlay replaces it with `#define ESPHOME_VERSION "2026.10.0"` and `VERSION_CODE(2026, 10, 0)` (RESEARCH Pattern 1). `defines.h` content is given in full in RESEARCH.md lines 240-278; copy it verbatim.

---

### `tests/unit/characterization/test_*.cpp` (all characterization tests)

**Analog for file layout and style:** `tests/unit/test_real_frames.cpp` and `tests/unit/test_frame_parser.cpp`.

**Header + includes** (`test_real_frames.cpp:1-9`):
```cpp
/// test_real_frames.cpp — Regression tests from real captured UART frames.
/// Deps: cn105_protocol.h, cn105_types.h
///
/// Source: Live debug logs from atom-s3 ESP32 (2026-05-02 14:04-14:05)
#include <gtest/gtest.h>
#include "cn105_protocol.h"
#include "cn105_types.h"

using namespace cn105_protocol;
```
New files: same `/// test_<topic>.cpp — <role>` + `/// Deps:` header; replace `TEST` with `TEST_F(Cn105Fixture, ...)` (first `TEST_F` use in repo; no existing analog, see No Analog). Suites PascalCase.

**Section banner** (`test_frame_parser.cpp:11-13`):
```cpp
// ════════════════════════════════════════════════════════════════
// Basic lifecycle
// ════════════════════════════════════════════════════════════════
```

**Real frame bytes as scripted RX** (`test_real_frames.cpp:14-16`, `:19-26`): hand-written `uint8_t pkt[] = {0xFC,0x5A,0x01,0x30,0x02, 0xCA,0x01};` without checksum, then `checksum(pkt, N)`. In the fixture, `feed()` appends checksum using `cn105_protocol::checksum` (RESEARCH "Don't Hand-Roll"; do not reimplement as `test_checksum.cpp` does). Do not invent frames beyond `test_real_frames.cpp`; flag to the user instead (CONTEXT specifics).

**Subject code for `test_cycle_management.cpp`** (`components/cn105/cycle_management.cpp`):
```cpp
// lines 33-36  (init)
void cycleManagement::init() { cycleRunning = false; lastCompleteCycleMs = CUSTOM_MILLIS; }
// lines 58-60 (BUG-01: absolute timestamps; strict '>' boundary, probe interval and interval+1)
if (CUSTOM_MILLIS < lastCompleteCycleMs) return false;
return (CUSTOM_MILLIS - lastCompleteCycleMs) > update_interval;
// lines 63-65
if (CUSTOM_MILLIS < lastCycleStartMs) return false;
return (CUSTOM_MILLIS - lastCycleStartMs) > (2 * update_interval) + 1000;
// lines 38-50 deferCycle(): delay = 750*2 under ESPHOME_LOG_LEVEL >= DEBUG (D-22 -> 1500 ms);
// lastCompleteCycleMs = CUSTOM_MILLIS + delay
```
Time seam: `Globals.h:11-12`: `#define CUSTOM_MILLIS esphome::millis()` / `#define CUSTOM_DELAY(x) esphome::delay(x)`. Tests drive it via `TestClock` linked in place of ESPHome HAL (RESEARCH Pattern 2).

**BUG-pin naming/comment (D-16)**, from RESEARCH.md lines 428-435:
```cpp
// Pins current buggy behaviour — flip in Phase 4 (TIME-01)
TEST_F(CycleManagementFixture, Characterize_BUG01_StallsAfterMillisWrap) { ... }
```
Required BUG pins: BUG-01 (cycle_management), BUG-03 (optimistic publish; two publishes observed per RESEARCH, in `test_climate_controls`/`test_hp_writings`), BUG-04 (single `pending_packet_`, `test_hp_writings`), BUG-07 (`soft_timeout_ms = 0`, `test_request_scheduler`), optionally BUG-02 (`hp_readings.cpp:84` `abs()` returns int) and BUG-05 (recorded `uart_set_pin` fallback 1/2).

**Error-message/regression comment style:** cite the issue/bug mechanism at the top, as in `test_setpoint_persistence.cpp:1-14` (Bug / Fix explanation paragraph).

---

### `tests/unit/characterization/fixture.{h,cpp}` (test fixture, event-driven)

**Analog:** none in repo (no `TEST_F`). Copy the verified skeleton from RESEARCH.md lines 380-426 (`FakeUart`, `Cn105Fixture`: SetUp re-creates `esphome::App` by placement-new, zero-filled storage for `CN105Climate`, `register_climate(hp_, "clim", 0, 0)`, `config_traits()` mirrored from `components/cn105/climate.py to_code`, `add_on_state_callback` recorder, `step()` = advance clock + `loop()` + `App.scheduler.call(millis())`). Needs UART configured 8E1 or `setupUART()` never reaches CONNECTING (Pattern 4). Observe HA state with real ESPHome entity callbacks (Pattern 5). Keep the fixture reusable by Phase 2 simulator (D-18).

---

### `tests/unit/characterization/test_profile_state.cpp`

**Analogs:** `tests/unit/test_profiles.cpp`, `tests/unit/test_lossnay.cpp`, `tests/unit/test_setpoint_persistence.cpp` for what profile state the pure tests already pin (use `HeatPumpProtocol` helpers to build expected values instead of hard-coding):
```cpp
using esphome::cn105::HeatPumpProtocol;
using SetpointState = HeatPumpProtocol::SetpointState;
auto s = HeatPumpProtocol::saved_setpoint_state(CLIMATE_MODE_HEAT_COOL, 20.0f, 24.0f);
```
(`test_setpoint_persistence.cpp:15-16, 25`). The characterization versions drive `HeatPumpProfile`/`LossnayProfile` through the real `CN105Climate` and assert bytes + published state (D-15).

---

### `.github/workflows/build.yaml` (modify)

**Analog:** the existing `unit-tests` job (lines 15-28) and `esphome-build` (`needs: unit-tests`, line 36).
```yaml
  unit-tests:
    name: Unit Tests (C++)
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v5

    - name: Configure
      run: cmake -B tests/unit/build -S tests/unit -DCMAKE_BUILD_TYPE=Release
    - name: Build
      run: cmake --build tests/unit/build -j$(nproc)
    - name: Run tests
      run: cd tests/unit/build && ctest --output-on-failure --no-tests=error
```
Changes: `runs-on: ubuntu-24.04` (D-20), `apt-get install g++-multilib`, `fetch-depth: 0` for diff-cover, call `tests/unit/coverage.sh --ci` (D-08), upload HTML/XML artifact, write `$GITHUB_STEP_SUMMARY`, add `permissions: pull-requests: write` for the sticky comment. Keep `esphome-build` `needs: unit-tests` (or the new gating job). Use `actions/checkout@v5` for consistency. No third-party actions (D-06).

---

### `tests/unit/coverage.sh`, `coverage_gate.py`, `test_coverage_gate.py`

No analog. Commands to copy are in RESEARCH.md lines 440-450 (gcovr per-build tracefile + merge, `--root` = repo root, `--filter components/cn105/`, `--exclude-directory '.*/CMakeFiles/.*'`, `--exclude-throw-branches --exclude-unreachable-branches`). Python headers follow the project convention `## <file>.py — <description>` / `## Role:` / `## Deps:`. Gate: exact counts from `--json-summary`, 0.1-point tolerance (D-12), stdlib only; self-test via `unittest` covers drop-fails/rise-ratchets (D-14).

## Shared Patterns

### File header comments
**Source:** `tests/unit/test_real_frames.cpp:1-4`, `test_frame_parser.cpp:1-4`. `/// <file> — <role>` then `/// Deps: ...`. Apply to every new C++ file. Comments in English.

### Test banners and naming
**Source:** `tests/unit/test_frame_parser.cpp:11-13`. `// ════` banners; suite names PascalCase; bug pins `Characterize_BUG<NN>_<Desc>` with a "flip in Phase N (REQ-ID)" comment.

### Production-code boundary (D-03/D-21)
`components/cn105/` must not change (no `#ifdef UNIT_TEST`, no new `friend`). Private access is via `-fno-access-control` on test TUs only. Pure-protocol headers stay free of ESPHome includes except `esphome/components/climate/climate_mode.h`.

### Include-path isolation
Existing tests use `tests/unit/mocks` + `CN105_SRC_DIR` include dirs (`CMakeLists.txt:26-28`); the harness must not see `mocks/` (shadowing). Existing 287 tests and `cn105_legacy_action_tests` stay unchanged.

### Log level and clock
Harness log level `ESPHOME_LOG_LEVEL_VERY_VERBOSE` (D-22); time only through the `TestClock` replacement of `esphome::millis()` (D-05).

## No Analog Found

| File | Role | Data Flow | Reason |
|---|---|---|---|
| `harness/platform/test_platform.*` | utility | event-driven | No test clock exists; use RESEARCH Pattern 2 |
| `characterization/fixture.*` | fixture | event-driven | No `TEST_F`/`App` harness exists; RESEARCH.md lines 380-426 |
| `harness/shim_esp32/**`, `shim_esp8266/**`, link stubs | shim | transform | Only the light `mocks/` exist; shim surface listed in RESEARCH "Platform Variants" |
| `coverage.sh`, `coverage_gate.py`, `coverage-baseline.json`, `requirements-coverage.txt` | tooling | batch | No coverage tooling or scripts exist yet; RESEARCH gcovr commands |
| Sticky PR comment / label bypass job | CI | request-response | No PR-comment job exists; use `gh api` with HTML marker (RESEARCH "Don't Hand-Roll") |

## Metadata

**Analog search scope:** `tests/unit/`, `.github/workflows/`, `components/cn105/cycle_management.cpp`, `components/cn105/Globals.h`
**Files scanned:** ~10
**Pattern extraction date:** 2026-10-27
