# Phase 1: Coverage Safety Net - Research

**Researched:** 2026-10-27
**Domain:** C++ host test harness for an ESPHome external component (real ESPHome core sources), gcov/gcovr coverage tooling, CI ratchet + diff-coverage gates, characterization tests
**Confidence:** HIGH (the harness, all three platform variants, the clock seam, gcovr/diff-cover behaviour and the BUG-01/BUG-02 reproductions were actually built and run in the scratch area; CI-runner specifics are MEDIUM)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Test harness (compiling `CN105Climate` under GoogleTest)**
- **D-01:** Use the **real ESPHome C++ core/headers**, fetched at a pinned version (e.g. CMake `FetchContent` of the esphome repo), compiled for the ESPHome `host` platform with a hand-written `esphome/core/defines.h` replacing codegen output. Not hand-written stubs. The existing `tests/unit/mocks/` stays for the current pure tests; the planner decides whether they migrate. — **Reversibility:** costly — every characterization test and the Phase 2 simulator/Phase 3 host job build on this harness; switching to stubs later means re-plumbing all of them.
- **D-02:** Pin the **exact ESPHome version CI `stable` uses today** (`ghcr.io/esphome/esphome:stable` = **2026.10.0** on 2026-10-27), bumped manually alongside the CI. Minimum-version (2025.7) paths stay covered only by CI config compilation.
- **D-03:** Production code changes in this phase are limited to **behaviour-neutral testability seams** (e.g. widening access, making a method virtual, splitting an include). **No `#ifdef UNIT_TEST` / test-only conditionals in shipped code.** Anything else is done on the test side (stubs at link level, `esphome::millis()` test clock, fake UART).
- **D-04:** Build **two test configurations: `USE_ESP32` and `USE_ESP8266`**, so the platform-specific branches (`std::mutex` vs `emulateMutex`, FreeRTOS includes) are both covered — Phase 7 removes the mutex and needs both sides pinned. Note for research: the ESPHome `host` platform normally defines `USE_HOST`, so the ESP32/ESP8266 variants need their platform defines and minimal FreeRTOS/ESP8266 shims on host; research must confirm feasibility and propose the shim surface.
- **D-05:** Time is driven through a test-side stub of `esphome::millis()` (what `CUSTOM_MILLIS` expands to in `components/cn105/Globals.h`), controllable by tests, including values near `UINT32_MAX`. The production clock abstraction is **not** introduced here (that is Phase 4, TIME-01/DEBT-07).

**Coverage tooling and reporting**
- **D-06:** **In-house only** — no Codecov/Coveralls, no third-party upload, no token. GCC `--coverage` + **gcovr**; Markdown summary written to `$GITHUB_STEP_SUMMARY`, HTML report uploaded as a workflow artifact, Cobertura XML produced for diff coverage.
- **D-07:** Diff coverage via **diff-cover** on the Cobertura XML against the PR base; result shown in the job summary **and** in a single PR comment that is updated in place (posted with the repository `GITHUB_TOKEN`, `pull-requests: write`). Repo is the user's fork `coredmp95/MitsubishiCN105ESPHome`.
- **D-08:** One local entry point: a script (e.g. `tests/unit/coverage.sh`) that configures a coverage build, runs all test configurations, and produces the same summary/HTML/XML as CI. **CI calls the same script**, so local and CI results match. Documented in `tests/unit/` README or the main README.
- **D-09:** Toolchain: **GCC + gcovr** (what `ubuntu-latest` provides). Clang/llvm-cov not used in this phase.

**Baseline and gates (ratchet)**
- **D-10:** Coverage scope = **all `.cpp`/`.h` under `components/cn105/`**; files never compiled into tests count as 0% (gcovr root/filter configured so they appear). Excluded: `climate.py`, `__init__.py`, anything under `tests/`. The initial baseline is expected to be low; that is accepted.
- **D-11:** Gate on **both line and branch coverage**, each with its own baseline value.
- **D-12:** Baseline stored in a **committed file `tests/unit/coverage-baseline.json`** (line %, branch %). CI fails if either metric drops by **more than 0.1 point** below the baseline. When coverage rises, CI reports the proposed new values and the PR author commits the updated baseline in the same PR (reviewed like code). No CI auto-commit to `main`.
- **D-13:** Diff coverage is **blocking at 80% of changed lines** (lines in `components/cn105/`). Exception mechanism: a **`coverage-exempt` PR label**, with the justification written in the PR description; the gate then reports but does not fail.
- **D-14:** Success criterion 2 requires demonstrating both outcomes once (a drop fails, a rise allows ratcheting) — e.g. a scripted self-test of the gate logic or a documented throwaway PR; planner chooses.

**Characterization tests**
- **D-15:** Assert **both the exact bytes written to the (fake) UART and the state published to HA** (climate mode/action, target/current temperatures, fan, swing/vanes, and the auxiliary entities touched) for each scenario.
- **D-16:** Tests that pin a known bug are **named after the audit ID and marked with a comment**, e.g. `CycleManagement.Characterize_BUG01_StallsAfterMillisWrap` with `// Pins current buggy behaviour — flip in Phase 4 (TIME-01)`. They pass today; the phase that fixes the bug flips them in the same PR. Greppable by `Characterize_BUG`. Pinned quirks at minimum: BUG-01 (absolute-timestamp comparisons / wrap stall), BUG-03 (optimistic publish + `wantedSettings` cleared after a merely queued write), BUG-04 (single `pending_packet_` slot overwritten by info requests), BUG-07 (`soft_timeout_ms = 0` stall, late response restarting the chain).
- **D-17:** Depth = **as much as possible, prioritized, in two PRs**:
  - **PR 1a (completes COV-01..04):** coverage tooling + gates + harness + characterization of the milestone's target paths: `cycle_management`, `request_scheduler`, `hp_writings` / `writePacket` / pending packet, `climateControls` (command → wantedSettings → publish), and state handling in the profiles (`heatpump_profile`, `lossnay_profile`).
  - **PR 1b (extension):** sensors, selects, switches, installer/hardware settings functions, Lossnay specifics, remote temperature path, remaining decode paths — raising the baseline.
  - Phase 2 may start once PR 1a is merged.
- **D-18:** Layout: **one file per module under `tests/unit/characterization/`** (`test_cycle_management.cpp`, `test_request_scheduler.cpp`, `test_hp_writings.cpp`, `test_climate_controls.cpp`, `test_profile_state.cpp`, …) sharing a **common `TEST_F` fixture** that builds a `CN105Climate` with a fake UART and the test clock. The fixture is designed for reuse by the Phase 2 simulator (SIM-02).

### Claude's Discretion
- Exact FetchContent mechanics, the content of the hand-written `defines.h`, and how ESPHome core sources are selected for the host build.
- Fake UART design (byte capture + scripted RX) and how HA-published state is observed (real ESPHome entity objects vs test listeners).
- gcovr options (exclusions, branch noise filtering such as `--exclude-throw-branches` / `--exclude-unreachable-branches`) as long as D-10/D-11 hold.
- CI job layout (extend the existing `unit-tests` job vs a separate `coverage` job), as long as the existing `esphome-build` matrix still depends on unit tests.
- Whether existing pure tests also run under the new harness or keep their current lightweight binary.

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| COV-01 | CI measures line and branch coverage of `components/cn105/` from the unit tests and publishes the report (job summary + artifact) | gcovr 8.6 `--markdown-summary` / `--html-nested` / `--json-summary` verified; gcno-only files report as 0% (D-10) verified; per-variant tracefile merge verified (see "Coverage tooling") |
| COV-02 | A coverage baseline is recorded and CI fails if total coverage drops below it (ratchet) | Gate prototype (stdlib Python, exact counts, 0.1-pt tolerance, 5 unit tests) built and passing; compiler-version sensitivity measured (GCC 13.3 vs 15.2 differ by 12 lines / 8 branches on identical tests) |
| COV-03 | Each PR reports coverage of changed lines (diff coverage) | diff-cover 10.6.0 on gcovr Cobertura verified on a toy repo (partial coverage -> exit 1; docs-only diff -> "No lines with coverage information", exit 0) |
| COV-04 | Characterization tests pin current behaviour of cycle_management, request_scheduler, hp_writings/writePacket, climateControls, profile state handling | Real `CN105Climate` + real ESPHome 2026.10.0 core built and driven end-to-end on 3 platform variants (handshake -> poll cycle -> ClimateCall -> 0x41 SET frame -> HA publishes); BUG-01 and BUG-02 reproduced on the unmodified production sources |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

Source: `/home/fabrice/dev/MitsubishiCN105ESPHome/.claude/CLAUDE.md` plus the user memory index.

- **Compatibility:** no breaking change to YAML keys / HA entities. (Phase 1 makes no shipped-behaviour change.)
- **Tests:** all existing unit tests keep passing (287 tests today, verified `100% tests passed, 0 tests failed out of 287`); every fixed bug gets a unit test (here: every *pinned* bug gets a `Characterize_BUG..` test).
- **Coverage:** coverage of `components/cn105/` never drops below the recorded baseline; changed lines are tested.
- **CI:** all 7 CI ESPHome configs (ESP32 + ESP8266) must still compile; `esphome-build` keeps `needs: unit-tests`.
- **Testability:** new logic in pure `*_protocol.h` style functions; **no new `friend`**, "expose a method instead" (see seam recommendation: `-fno-access-control` on test TUs avoids touching `cn105.h`).
- **Platform:** ESPHome >= 2025.7 supported via `esphome_compat.h`; the pinned harness covers 2026.10.0 only (D-02).
- **Conventions for new files:** snake_case file names (`test_<topic>.cpp`); header comment `/// <file> — <role>` + `/// Deps:`; comments in English; log tags as named constants; `enum class` for enums; new test suites PascalCase; section banners `// ════...` in tests; reference issue numbers in regression comments.
- **Delivery:** the user's fork `coredmp95/MitsubishiCN105ESPHome` only (never upstream `echavet`), branches based on `origin/main`; one PR per phase (CONTEXT D-17 overrides to two PRs, 1a / 1b for this phase).
- **Docs:** README / `tests/unit/` README documents the local coverage command (D-08).
- **Language:** reply to the user in French, files in English. Flag when real CN105 captures are needed (see "Real-capture flags").

## Summary

The whole feasibility question is answered by experiment: **the real, unmodified `components/cn105/*.cpp` (all 17 translation units) compiles, links and runs against the real ESPHome 2026.10.0 C++ core on the Linux host, in three platform configurations (`USE_HOST`, `USE_ESP32` with a FreeRTOS/ESP-IDF shim, `USE_ESP8266` with an Arduino shim), with GCC 15 locally and GCC 13.3 on `ubuntu:24.04` (the CI runner family), also with `-m32`.** I drove a real `CN105Climate` through the CONNECT handshake (`FC 5A 01 30 02 CA 01 A8` written after the 10 s grace delay, reply `FC 7A 01 30 01 00 54` accepted), a full 0x02/0x03/0x06/0x09/0x04 poll cycle fed with frames from `test_real_frames.cpp`, then a `climate.make_call().set_mode(HEAT).set_target_temperature(23).perform()` which produced the SET frame `FC 41 01 30 10 01 07 00 01 01 00 00 00 00 00 00 00 00 00 AE 00 C6` and two HA publishes (the optimistic one at `control()` time, another at send time = BUG-03 observable). No production file had to be edited.

The harness needs: (1) a hand-written overlay `esphome/core/defines.h` + `version.h` + `esphome.h` placed *before* the fetched ESPHome root on the include path and force-included (`-include esphome/core/defines.h`, as ESPHome's own PCH does), (2) ~31 ESPHome `.cpp` files compiled per platform variant, (3) test-side link stubs: the clock (`esphome::millis()` & friends), logging (`esp_log_printf_`), preferences, and per-platform HAL bits, (4) a `FakeUart : uart::UARTComponent`, (5) re-creation of the global `esphome::App` per test via placement-new. Access to the many private members the characterization needs is solved **on the test side with GCC `-fno-access-control`** (verified) — zero edit to `cn105.h`. The one fidelity trap found is **LP64 vs ILP32**: on x86-64 `unsigned long` is 64 bits, on ESP32/ESP8266 it is 32; the production code mixes `unsigned long`, `long` and `uint32_t` timestamps (BUG-08). **Build the characterization harness with `-m32`** (verified on Ubuntu 24.04: ESPHome lib + cn105 + GoogleTest + the 287 existing tests all work).

Coverage: gcovr 8.6 reads `.gcno` files without `.gcda`, so *compiling* every cn105 `.cpp` with `--coverage` is enough for D-10 (verified: an unlinked file shows 0%). Header-only code appears only if a compiled TU instantiates it. A real denominator is ~2,950-3,000 executable lines / ~2,000 branches (versus 565 lines / 519 branches that the existing pure tests currently see), so the first honest baseline is low (~38 % lines / ~25 % branches with just a smoke scenario on two variants, 18 % lines with the existing tests alone). **Coverage numbers depend on the GCC version** (measured: same tests give 565/527 lines & 519/423 branches on GCC 15.2 but 577/524 & 519/415 on GCC 13.3): the baseline must be produced by the CI toolchain (pin `ubuntu-24.04`, install `g++-multilib`), and the local script needs a `--docker` mode (`ubuntu:24.04`) to give CI-identical numbers.

**Primary recommendation:** Build the harness as an overlay-based CMake sub-project (FetchContent `esphome` tag `2026.10.0`, `GIT_SHALLOW`), compile the unmodified cn105 sources + ~31 ESPHome sources per platform variant (ESP32-shim, ESP8266-shim; HOST optional) with `-m32 -std=gnu++20 --coverage -O0 -fno-access-control` on the test targets only, drive time with one `TestClock` linked in place of ESPHome's HAL, and gate with gcovr tracefile merge + a stdlib-Python ratchet script + diff-cover, all run by one `tests/unit/coverage.sh` that CI and developers share (CI on `ubuntu-24.04`, developers via `--docker`).

## Architectural Responsibility Map

(Adapted: this phase is test infrastructure, not a product feature. "Tier" = where the capability lives in the repo / pipeline.)

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Unmodified production code under measurement | `components/cn105/` (shipped) | — | D-03: no behaviour change; only compiled by tests |
| ESPHome core build + overlay `defines.h`/`version.h`/`esphome.h` | `tests/unit/harness/` (test-side, CMake) | — | Replaces ESPHome codegen output; pinned to 2026.10.0 |
| Platform shims (FreeRTOS/ESP-IDF, Arduino) and link stubs (clock, log, prefs) | `tests/unit/harness/platform/` | — | Link-level substitution, no `#ifdef` in shipped code (D-03/D-05) |
| Fake UART, fixture, frame builders | `tests/unit/characterization/` fixture | Phase 2 simulator reuses | D-18 |
| Characterization tests | `tests/unit/characterization/test_*.cpp` | — | One file per module |
| Coverage build + reports | `tests/unit/coverage.sh` | CI job | One entry point, same toolchain (D-08) |
| Ratchet gate + self-test | `tests/unit/tools/coverage_gate.py` (+ unittest) | CI step | Pure Python, hermetic, testable |
| Diff coverage + PR comment + label exemption | `.github/workflows/build.yaml` (new job) | `gh` CLI with `GITHUB_TOKEN` | D-07/D-13 |
| Baseline record | `tests/unit/coverage-baseline.json` (committed) | — | D-12 |

## Standard Stack

### Core

| Library / tool | Version | Purpose | Why Standard |
|----------------|---------|---------|--------------|
| ESPHome C++ sources | tag `2026.10.0` (commit `704551a6c93c29a311802f7ee97d67a9e9bce718`) | Real core/components under test (D-01/D-02) | `[VERIFIED: git ls-remote + GitHub releases API]` tag exists, release is `"immutable":true`, published 2026-10-21, equals Docker `stable` (last pushed 2026-10-21) |
| GoogleTest | v1.15.2 (already used) | Test framework, `TEST_F` | Existing; verified to build and pass under `-m32` |
| GCC + gcov | 13.3.0 on `ubuntu-24.04` (CI) / 15.2 locally | Compiler + coverage (D-09) | `[VERIFIED: docker ubuntu:24.04]` compiles ESPHome 2026.10 TUs + cn105 with `-std=gnu++20`; runner image README lists `g++ 4:13.2.0-7ubuntu1` |
| `g++-multilib` | apt, Ubuntu 24.04 | `-m32` ILP32 fidelity | `[VERIFIED: docker]` ESPHome lib + cn105 + gtest + 287 tests build/run under `-m32` |
| gcovr | **8.6** (2026-01-13) | gcov driver; summary/HTML/Cobertura/JSON | `[VERIFIED: pip + PyPI JSON]`; 8.7 exists (2026-10-16) but is 11 days old; pin 8.6 |
| diff-cover | **10.6.0** (2026-09-22) | Changed-line coverage from Cobertura | `[VERIFIED: pip + PyPI JSON]`; 11.0.0 is published today, pin 10.6.0 |
| CMake | >= 3.14 (runner has 3.31.6; local 4.2.3) | Build | `FetchContent_MakeAvailable` on a non-CMake repo (esphome) works; shallow tag clone took 7.7 s |
| Python 3 (stdlib only) | 3.12 (runner) | Ratchet gate script + `unittest` self-test | No extra dependency |

### Supporting

| Tool | Version | Purpose | When to Use |
|------|---------|---------|-------------|
| `gh` CLI | preinstalled on runners `[ASSUMED]` | Find/update the sticky PR comment, read live PR labels | `coverage-gate` job |
| `actions/checkout` | v5 already used (v7.0.1 is latest `[VERIFIED: GitHub API]`) | `fetch-depth: 0` for diff-cover | keep v5 for consistency or bump all together |
| `actions/upload-artifact` | v7.0.2 `[VERIFIED: GitHub API]` | HTML + XML + JSON artifact | coverage job |
| `actions/setup-python` | v7.1.0 `[VERIFIED: GitHub API]` | pip with pinned `requirements` | optional; runner already has python 3.12 |
| Docker | local 29.9.2 | `coverage.sh --docker` toolchain parity | developers |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Overlay-only (`-I overlay -I <fetched root>`) | Copy a 178-file subset into a staged tree | Overlay works without any copy/patch (verified: `-I overlay` placed before the ESPHome root). Staging also verified (1.5 MB) but is unnecessary |
| `-fno-access-control` on test TUs | `private:` -> `protected:` at `cn105.h:391` + derived test class | Both verified feasible. Flag is GCC-only (D-09 says GCC) and touches no shipped file; the one-word edit is the D-03-sanctioned fallback |
| `-m32` harness | native 64-bit | 64-bit is simpler but `unsigned long`/`long` are 8 bytes there, so wrap arithmetic (BUG-01/BUG-08) is not pinned as it behaves on ESP32/ESP8266 |
| True `USE_ESP8266` shim variant | HOST variant (cn105 sees `!USE_ESP32`, i.e. identical cn105 code path to ESP8266) | HOST needs no shim, ESP8266 shim is ~6 headers / ~15 stubs and works; see Open Question 1 |
| gcovr | lcov + genhtml / fastcov | gcovr gives Cobertura (diff-cover input), Markdown summary and JSON in one tool |
| Codecov/Coveralls | — | Excluded by D-06 |

**Installation (CI and local):**
```bash
sudo apt-get install -y g++-multilib cmake          # CI: ubuntu-24.04
python3 -m venv .venv && .venv/bin/pip install -r tests/unit/requirements-coverage.txt
# tests/unit/requirements-coverage.txt
#   gcovr==8.6
#   diff-cover==10.6.0
```

**Version verification:**
```
$ pip index versions gcovr      -> gcovr (8.7)       ; releases 8.5 2026-01-08, 8.6 2026-01-13, 8.7 2026-10-16
$ pip index versions diff-cover -> diff-cover (11.0.0); releases 10.6.0 2026-09-22, 11.0.0 2026-10-27
$ git ls-remote --tags https://github.com/esphome/esphome.git | grep 2026.10  -> 704551a6... refs/tags/2026.10.0
```

## Package Legitimacy Audit

| Package | Registry | Age | Downloads | Source Repo | Verdict | Disposition |
|---------|----------|-----|-----------|-------------|---------|-------------|
| gcovr | PyPI | first release 2010-09-01 | not reported by the seam | github.com/gcovr/gcovr (PyPI `project_urls`) | **SUS** (seam: `too-new`, `unknown-downloads`, `no-repository`) | Keep, pinned to 8.6; **planner adds a `checkpoint:human-verify` before first install** |
| diff-cover | PyPI | first release 2013-06-28 | not reported by the seam | github.com/Bachmann1234/diff-cover | **SUS** (seam: `too-new`, `unknown-downloads`) | Keep, pinned to 10.6.0; **planner adds a `checkpoint:human-verify` before first install** |

The SUS verdicts come from the seam's recency heuristic (latest release published 2026-10-16 / 2026-10-27) and missing download stats; PyPI metadata shows both are long-lived projects with public repositories (`[VERIFIED: pypi.org/pypi/<pkg>/json]`). That is why the recommendation pins the older releases (8.6, 10.6.0). Python packages have no `postinstall` step. Package names were confirmed against the Python registry (`pip index versions`), not just npm.

**Packages removed due to [SLOP] verdict:** none
**Packages flagged as suspicious [SUS]:** gcovr, diff-cover (planner inserts `checkpoint:human-verify` before the first install)

Non-package supply chain: the ESPHome sources (git tag, immutable release), GoogleTest (existing FetchContent), GitHub first-party actions only. **No third-party GitHub Action is needed** (the sticky comment is done with `gh api`), consistent with D-06.

## Architecture Patterns

### System Architecture Diagram

```
                    ┌────────────────────── tests/unit/coverage.sh (one entry point) ──────────────────────┐
                    │  local: --docker (ubuntu:24.04)         CI: runs the same script on ubuntu-24.04      │
                    └──────────────────────────────────────────────────────────────────────────────────────┘
                                             │ cmake -m32 --coverage -O0
                                             ▼
 FetchContent esphome@2026.10.0 ──► overlay/ (defines.h, version.h, esphome.h, wifi_component.h)   [-I overlay -I esphome-src, -include defines.h]
        │                                    │
        ▼                                    ▼
 31 ESPHome .cpp  ───────────────►  static lib  esphome_<variant>      variants: esp32 (+shim32/), esp8266 (+shim8266/), [host]
 (core+climate+sensor+select+...)             │
                                              │      platform stubs (link level): TestClock{millis,millis_64,micros,delay,
                                              │      esp_timer_get_time,vTaskDelay...}, log capture, in-memory preferences,
                                              │      FreeRTOS/IDF/Arduino stubs, recording driver/uart.h
 components/cn105/*.cpp (UNMODIFIED, --coverage) ──► object lib cn105_<variant>
                                              │
        ┌─────────────────────────────────────┴──────────────────────────────────────┐
        ▼                                                                            ▼
 characterization exe per variant:                                          existing cn105_tests + cn105_legacy_action_tests
  Fixture{ TestClock, App re-create, FakeUart, CN105Climate in zeroed storage,       (C++17, mocks/, unchanged)
           state-callback recorder, frame builders }
  test_cycle_management / request_scheduler / hp_writings / climate_controls / profile_state
        │  ctest                                                                       │
        └───────────────► .gcda/.gcno ◄─────────────────────────────────────────────────┘
                                   │ gcovr --json per build dir (filter components/cn105/)
                                   ▼
                       gcovr -a tf1 -a tf2 ... ──► summary.json · cobertura.xml · markdown-summary · html-nested
                                   │
            ┌──────────────────────┼───────────────────────────────┐
            ▼                      ▼                               ▼
   coverage_gate.py          diff-cover (PR only)           $GITHUB_STEP_SUMMARY + artifact
   (baseline JSON,           --compare-branch origin/BASE   + sticky PR comment (gh api, marker)
    0.1 pt, RATCHET msg)     --fail-under 80, label bypass
```

### Recommended Project Structure
```
tests/unit/
├── CMakeLists.txt                 # existing; add `option(CN105_HARNESS ON)` + add_subdirectory(harness) BEFORE include_directories(mocks)
├── coverage.sh                    # D-08 single entry point (--ci, --docker, --update-baseline)
├── coverage-baseline.json         # D-12 {"line":..,"branch":..,"gcc":..,"gcovr":..,"esphome":"2026.10.0"}
├── requirements-coverage.txt      # gcovr==8.6, diff-cover==10.6.0
├── README.md                      # how to run, how to ratchet, how to bump ESPHome
├── tools/
│   ├── coverage_gate.py           # ratchet logic (stdlib only)
│   └── test_coverage_gate.py      # D-14 self-test (unittest)
├── harness/
│   ├── CMakeLists.txt             # FetchContent esphome, per-variant libs, flags
│   ├── overlay/                   # esphome.h, esphome/core/{defines,version}.h, esphome/components/wifi/wifi_component.h
│   ├── shim_esp32/                # freertos/*, driver/*, esp_*.h, sdkconfig.h, soc/soc_caps.h
│   ├── shim_esp8266/              # pgmspace.h, c_types.h, core_esp8266_features.h, Esp.h, WString.h, coredecls.h
│   └── platform/                  # test_clock.{h,cpp}, test_log.cpp, test_prefs_<v>.cpp, stubs_esp32.cpp, stubs_esp8266.cpp
├── characterization/
│   ├── fixture.{h,cpp}            # Cn105Fixture (TEST_F), FakeUart, frame helpers
│   ├── test_cycle_management.cpp
│   ├── test_request_scheduler.cpp
│   ├── test_hp_writings.cpp
│   ├── test_climate_controls.cpp
│   └── test_profile_state.cpp     # PR 1b adds: test_sensors..., test_remote_temperature..., test_hardware_settings...
└── mocks/                         # unchanged (existing pure tests only)
```

### Pattern 1: Overlay + force-include instead of patching ESPHome
**What:** put `harness/overlay` first on the include path and force-include its `defines.h`; ESPHome's own `esphome/core/defines.h` in the fetched tree is a static-analysis dummy (comment: "This file is not used by the runtime ... only used by static analyzers and IDEs"), and its `version.h` says `ESPHOME_VERSION_CODE VERSION_CODE(2099, 12, 0)`.
**When to use:** always. Without the force-include, headers such as `climate_mode.h` -> `log.h` define `ESPHOME_LOG_LEVEL` as `NONE` *before* `defines.h` is seen (verified failure mode); ESPHome solves this with `pch_prefix.h`, we solve it with `-include`.
**Example (verified minimal set; `esphome/core/defines.h`):**
```cpp
#pragma once
#include "esphome/core/macros.h"
#define ESPHOME_BOARD "unit-test"
#define ESPHOME_VARIANT "HOST"
#ifndef ESPHOME_LOG_LEVEL
#define ESPHOME_LOG_LEVEL ESPHOME_LOG_LEVEL_VERY_VERBOSE   // see Pitfall 7 (DEFER delay depends on it)
#endif
#define ESPHOME_LOGGER_TX_BUFFER_SIZE 512
#if defined(USE_ESP8266)
#define ESPHOME_THREAD_SINGLE
#else
#define ESPHOME_THREAD_MULTI_ATOMICS
#endif
#define USE_BINARY_SENSOR
#define USE_BUTTON
#define USE_CLIMATE
#define USE_NUMBER
#define USE_SELECT
#define USE_SENSOR
#define USE_SWITCH
#define USE_TEXT_SENSOR
#define USE_ENTITY_DEVICE_CLASS
#define USE_ENTITY_ICON
#define USE_ENTITY_UNIT_OF_MEASUREMENT
#define USE_WIFI                                   // real firmware has wifi; enables WAIT_WIFI branch (needs wifi_component.h stub)
#define ESPHOME_COMPONENT_COUNT 64
#define ESPHOME_ENTITY_BINARY_SENSOR_COUNT 4
#define ESPHOME_ENTITY_BUTTON_COUNT 4
#define ESPHOME_ENTITY_CLIMATE_COUNT 2
#define ESPHOME_ENTITY_NUMBER_COUNT 4
#define ESPHOME_ENTITY_SELECT_COUNT 8
#define ESPHOME_ENTITY_SENSOR_COUNT 16
#define ESPHOME_ENTITY_SWITCH_COUNT 8
#define ESPHOME_ENTITY_TEXT_SENSOR_COUNT 8
#ifdef USE_HOST
#define USE_ESPHOME_HOST_MAC_ADDRESS {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}
#endif
```
`version.h`: `#define ESPHOME_VERSION "2026.10.0"` / `#define ESPHOME_VERSION_CODE VERSION_CODE(2026, 10, 0)`. Do **not** define `ESPHOME_PROJECT_NAME` (application.cpp then needs `ESPHOME_PROJECT_VERSION`). `esphome.h` overlay includes: macros, defines, application, component, hal, helpers, log, preferences, uart, sensor, binary_sensor, text_sensor, select, switch, number, button, climate (the production code does `#include <esphome.h>` in `Globals.h:8` and `request_scheduler.cpp`).

### Pattern 2: Test clock at link level (D-05)
**What:** `uint32_t millis();` is an **out-of-line declaration on all three platforms** (`host/hal.h`, `esp32/hal.h`, `esp8266/hal.h`), so one definition in the test platform lib replaces it with no change to shipped code. `Globals.h:11` — `#define CUSTOM_MILLIS esphome::millis()` — and `Globals.h:12` — `#define CUSTOM_DELAY(x) esphome::delay(x)` — both flow through it.
- Keep one 64-bit `g_now_ms`; `millis()` returns `(uint32_t)g_now_ms`; set it to e.g. `0xFFFFFF00` and step past `2^32` to wrap.
- `delay(ms)` must **advance** the clock (deterministic, no sleeping): host `esphome::delay`, ESP32 `vTaskDelay`, ESP8266 `esphome::delay`.
- Per-platform extra hooks: host — `millis_64()`, `micros()`; ESP32 — `esp_timer_get_time()` (drives inline `micros()`/`millis_64()`), `xTaskGetTickCount`; ESP8266 — `::millis()`, `::micros()` (ESP8266 `millis_64()` is `Millis64Impl::compute(millis())`, so rollover tracking is exercised for free).
- The ESPHome scheduler (`Component::set_timeout`) runs off the same clock: drive it with `App.scheduler.call(esphome::millis())` after each clock step (verified: "checkFirstConnection" and soft timeouts fire).

### Pattern 3: Re-create `esphome::App` per test
`App` is storage in `application.cpp` constructed only by the generated `setup()`. In tests: `new (&esphome::App) esphome::Application();` in `SetUp()` and `esphome::App.~Application();` in `TearDown()` (verified 3 rounds). Entity names: `App.register_climate(&c, "clim", 0, 0)` (4-arg form calls the protected `configure_entity_`); same for sensors/selects/switches. LeakSanitizer reports ESPHome scheduler pool items on teardown (upstream never destroys `App`): irrelevant now, but Phase 7 (CI-02 ASan) must set `ASAN_OPTIONS=detect_leaks=0` for this binary or free the pool.

### Pattern 4: `CN105Climate` in zero-filled storage
The constructor does not initialise `lastReconnectTimeMs`, `update_interval_`, `debounce_delay_`, `data`, ... (BUG-08). Construct with placement-new into `memset(0)` storage so characterization is deterministic; `lastResponseMs` is set by `setup()`. Mirror codegen (`climate.py to_code`): `parent.set_data_bits(8)`, `set_parity(EVEN)`, `set_stop_bits(1)` (else `setupUART()` logs "not configured as SERIAL_8E1" and never reaches CONNECTING), `set_update_interval`, `config_traits().add_supported_mode/fan_mode/swing_mode`.

### Pattern 5: Observing "published to HA" with real objects
`Climate::add_on_state_callback` (and `Sensor`/`TextSensor::add_on_state_callback`) record each publish with a snapshot of `mode, action, target_temperature(_low/_high), current_temperature, fan_mode, swing_mode`. Publishes are cheap (`publish_state()` only logs and calls the callback list when `USE_CONTROLLER_REGISTRY` is undefined). This satisfies D-15 without test-only production code.

### Pattern 6: Platform variants as separate libraries (never mix `-D` per TU)
`UARTComponent::load_settings` is pure virtual under `USE_ESP32 || USE_ESP8266`; `Mutex` and `hal.h` also differ. Compile the ESPHome lib **and** cn105 **and** tests of one variant with identical defines. Three variants = three static libs, three executables (all linking the same unmodified cn105 sources, compiled once per variant).

### Anti-Patterns to Avoid
- **Adding `tests/unit/mocks` to the harness include path:** `mocks/esphome/core/version.h` and `mocks/esphome/components/climate/climate_mode.h` would shadow the real headers. `tests/unit/CMakeLists.txt` currently uses a directory-scoped `include_directories(${CMAKE_SOURCE_DIR}/mocks)` that is inherited by any later `add_subdirectory`; either call `add_subdirectory(harness)` before it or convert it to `target_include_directories` on the existing targets.
- **Global `CMAKE_CXX_FLAGS=--coverage`:** CMake's compiler-id try-compile leaves `.gcno` files (`CMakeFiles/3.28.3/CompilerIdCXX/a-CMakeCXXCompilerId.gcno` on CMake 3.28) that make gcovr abort with "GCOV could not find source file" (reproduced). Put `--coverage` on the cn105 object libs/tests only, and pass `--exclude-directory '.*/CMakeFiles/.*'` as a safety net.
- **`#define private public`:** use `-fno-access-control` on test TUs instead.
- **Compiling ESPHome's `core/log.cpp`:** it only forwards to a `Logger`; replace it with a capturing `esp_log_printf_`/`esp_log_vprintf_` (plus the `__FlashStringHelper*` overload on ESP8266) so tests can assert warnings and dump logs on failure.
- **Using ESPHome's `host/preferences.cpp`:** file-backed; use an in-memory store per variant.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Coverage aggregation, Markdown/HTML/Cobertura/JSON | custom gcov parser | gcovr 8.6 | handles `.gcno`-only files, multi-variant tracefile merge, filters |
| Changed-line coverage | custom git-diff parser | diff-cover 10.6.0 | handles merge-base, ignores non-covered files, Markdown output |
| Sticky PR comment | third-party action | 12-line `gh api` script with an HTML marker | no third-party action, GITHUB_TOKEN only |
| Frame checksums / frame building in tests | re-implementation in test code | `cn105_protocol::checksum` (`cn105_protocol.h:23`) and `FrameParser` | pure, already tested (CLEAN-04 notes `test_checksum.cpp` currently re-implements it; do not repeat) |
| ESPHome entities (Sensor, Select, Switch, Climate) | hand-written fakes | the real ESPHome classes compiled in the harness (D-01) | exact production behaviour, `publish_state` side effects |
| Test doubles for the scheduler | fake `set_timeout` | the real `Scheduler` + `App.scheduler.call(now)` on the test clock | same timer semantics as firmware |

**Key insight:** the expensive, fragile part is the ESPHome platform surface, not cn105; keep it confined to `harness/` (overlay + shims + stubs) so a version bump is one directory.

## Platform Variants: verified build recipe and shim surface (D-04)

All three compile and run `CN105Climate` end-to-end (CONNECT frame written at t=10000 ms in each). Numbers below are from the working experiments.

**ESPHome sources compiled per variant (all verified to link; the first row may be pruned further):**
- Core (all variants): `core/{alloc_helpers,application,component,entity_base,helpers,scheduler,time_64,util,gpio,time}.cpp`
- Components (all variants): `climate/{climate,climate_mode,climate_traits}`, `sensor/{sensor,filter}`, `binary_sensor/{binary_sensor,filter}`, `text_sensor/{text_sensor,filter}`, `select/{select,select_call,select_traits}`, `switch/switch`, `number/{number,number_call,number_traits}`, `button/button`, `uart/{uart_component,uart}`
- Variant-specific: HOST `core/wake/wake_host.cpp` + `components/host/helpers.cpp`; ESP32 `core/wake/wake_freertos.cpp`; ESP8266 `core/wake/wake_esp8266.cpp`
- **Not compiled:** `core/log.cpp` (replaced by capture), `core/static_task.cpp`, `core/color.cpp`, `core/component_iterator.cpp`, `components/host/{hal,core,preferences}.cpp` (replaced by stubs), all `esp32/`, `esp8266/` platform `.cpp`, `uptime`, `wifi`.
- Build time (16-core dev box, -j4, -O0): ESPHome lib ≈ 4 s, cn105 with `--coverage` ≈ 4 s per variant; expect well under 1 minute per variant on a 4-core runner.

**ESP32 variant (`-DUSE_ESP32`, `-I shim_esp32`):** 21 shim headers (about 110 lines): `freertos/{FreeRTOS,task,semphr}.h`, `sdkconfig.h`, `esp_attr.h`, `esp_system.h`, `esp_heap_caps.h`, `esp_cpu.h`, `esp_task_wdt.h`, `esp_err.h`, `esp_log.h` (empty), `esp_rom_crc.h`, `esp_chip_info.h`, `esp_ota_ops.h`, `esp_bootloader_desc.h`, `esp_flash.h`, `spi_flash_chip_{generic,mxic}.h` (empty), `soc/soc_caps.h` (empty), `driver/{uart,gpio}.h`. Link stubs (about 45 `extern "C"` one-liners): FreeRTOS (`xPortInIsrContext`, `vPortYield`, `vTaskDelay`, `xTaskGetTickCount`, `xTaskNotify*`, `xSemaphore{CreateMutex,Take,Give}`, `xTaskGetCurrentTaskHandle`, `esphome_main_task_handle`), IDF (`heap_caps_*`, `esp_system_abort`, `esp_cpu_get_cycle_count`, `esp_task_wdt_reset`, `esp_chip_info`, `esp_ota_get_bootloader_description`, `esp_rom_crc16_{le,be}`, `esp_err_to_name`, `esp_timer_get_time`) plus `esphome::{millis,arch_init,arch_get_cpu_freq_hz,arch_restart,get_mac_address_raw,random_bytes}` and the preferences trio (`ESP32PreferenceBackend::{save,load}`, `ESP32Preferences::make_preference`, `global_preferences`). **Bonus:** make `driver/uart.h`/`gpio.h` stubs *record* calls (`uart_param_config`, `uart_set_pin(port,tx,rx)`, `gpio_reset_pin`, ...): this pins `force_low_level_uart_reinit` (`cn105.cpp:416`, port fallback `UART_NUM_0`, pins fallback 1/2 = BUG-05) for Phase 5/LINK-05.

**ESP8266 variant (`-DUSE_ESP8266 -include pgmspace.h`, `-I shim_esp8266`):** 6 shim headers: `pgmspace.h` (`PSTR`, `F()`, `pgm_read_*`, the `*_P` libc aliases incl. `strcasecmp_P`/`strncasecmp_P`, `PGM_P`), `c_types.h`, `core_esp8266_features.h` (`F_CPU`, `esp_get_cycle_count`), `Esp.h` (`EspClass ESP`, `esp_schedule`, `esp_delay(unsigned long, bool(*)())`), `WString.h` (global `class __FlashStringHelper;`), `coredecls.h`. **`pgmspace.h` must be force-included first** (otherwise `<cstring>` is first included inside `namespace esphome` and breaks). Link stubs (about 15): `esphome::{millis,delay,arch_restart,get_mac_address_raw,random_uint32}`, `::{yield,micros,millis}`, `esp_schedule`, `esp_delay`, `system_soft_wdt_feed`, `esp_get_cycle_count`, `EspClass` methods, ESP8266 preferences trio. Note `Globals.h:4` includes `Arduino.h` when `ARDUINO >= 100`; the shim does not define `ARDUINO`, which is harmless here.

**HOST variant (`-DUSE_HOST`, no shim):** needs only the clock/hal stubs + `host/helpers.cpp`. Because `cn105` only tests `#ifdef USE_ESP32` (never `USE_ESP8266`; checked in `cn105.h:39,335,431,455`, `cn105.cpp:3,87,417`, `hp_writings.cpp:261`, `climateControls.cpp:140`, `utils.cpp:95,181,210,380,470`), the HOST variant executes exactly the code the ESP8266 build executes (`emulateMutex`, no low-level UART reinit). It is a free cross-check, not required by D-04.

**What the shims do NOT model:** real Arduino/IDF semantics. They are structurally faithful only for what ESPHome core and cn105 touch. When the pinned ESPHome version is bumped, expect small shim edits (new `#include`s in core files); that is the cost of D-02.

## Common Pitfalls

### Pitfall 1: LP64 vs ILP32 changes the pinned arithmetic
**What goes wrong:** on x86-64 `unsigned long`/`long` are 64-bit (measured `sizeof(unsigned long)=8`), so `uint32_t - unsigned long` and `long lrTimeMs = CUSTOM_MILLIS - lastResponseMs` (`cn105.cpp:395`) do not behave as on ESP32/ESP8266 where they are 32-bit. Characterization pinned on 64-bit would be wrong for the real targets and would "flip" incorrectly in Phase 4.
**How to avoid:** compile the characterization harness with `-m32` (`-DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32`, both C and CXX or CMake's try-compile fails). Verified on Ubuntu 24.04 with `g++-multilib`: ESPHome lib, cn105, GoogleTest, the existing 287 tests, and `--coverage` + gcovr all work.
**Warning signs:** a test that passes on the dev box (64-bit) and fails in the CI image, or vice versa.

### Pitfall 2: Coverage numbers depend on the GCC version
**What goes wrong:** identical sources + identical tests: GCC 15.2 -> 565 lines (527 hit) / 519 branches (423 hit); GCC 13.3 -> 577 lines (524 hit) / 519 branches (415 hit) [`VERIFIED: ran both`]. A baseline recorded locally would immediately fail or drift in CI (and `ubuntu-latest` moves to Ubuntu 26.04 "in November 2026" per the runner-images README).
**How to avoid:** CI job `runs-on: ubuntu-24.04` (not `latest`), baseline taken from the CI run (artifact proposes the JSON), local `coverage.sh --docker` runs `ubuntu:24.04` for CI-identical numbers; native local runs are informational. Record `gcc`, `gcovr`, `esphome` versions inside `coverage-baseline.json` and make the gate print a warning when they differ.

### Pitfall 3: Uncompiled files and header-only code (D-10)
**What goes wrong:** a `.cpp` that is not compiled with `--coverage` is invisible. Header-only entities (`*_sensor.h`, `*_select.h`) appear only when a compiled TU instantiates their inline code.
**How to avoid:** every `components/cn105/*.cpp` must be part of a coverage-instrumented object library (verified: a `.cpp` compiled but not linked reports as 0%). Add a scope check to `coverage.sh`: list `components/cn105/*.cpp` and fail if any is missing from the gcovr JSON. Document that header-only classes enter the denominator when first used by a test (PR 1b is where they become visible).

### Pitfall 4: Merging variants
`--object-directory` given twice keeps only the last; merge by writing one `--json` tracefile per build dir and combining with `gcovr -a tf1 -a tf2 ...` (verified: union of lines, `2980` lines vs `2944`/`2941` single-variant). Default `--merge-mode-functions=strict` did not complain for two variants; if it does for three, switch to another documented mode.

### Pitfall 5: `esphome::App` is not constructed
Any `set_timeout` before `new (&App) Application()` segfaults in `Mutex::lock` (reproduced). Do it in the fixture, never in a global.

### Pitfall 6: `hasUpdateIntervalPassed(...)` strictness and defer timing
`hasUpdateIntervalPassed` uses `>` (strict) (`cycle_management.cpp:59`: `return (CUSTOM_MILLIS - lastCompleteCycleMs) > update_interval;`), `doesCycleTimeOut` uses `> (2 * update_interval) + 1000` (`:64`). Boundary tests must probe `interval` and `interval+1`.

### Pitfall 7: `deferCycle()` delay depends on the log level (a hidden quirk to pin)
`cycle_management.cpp:26-30` — `#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_DEBUG` -> `uint32_t delay = DEFER_SCHEDULE_UPDATE_LOOP_DELAY * 2;` else `DEFER_SCHEDULE_UPDATE_LOOP_DELAY`, and `cn105_types.h:32` is `static const int DEFER_SCHEDULE_UPDATE_LOOP_DELAY = 750;`. So firmware deferral is 1500 ms at DEBUG/VERBOSE and 750 ms at INFO (the examples use both). The harness fixes one level (recommend `VERY_VERBOSE`, which compiles every `ESP_LOGV` argument list into the coverage denominator and exercises them); characterize the 1500 ms value and add a comment that the INFO-level 750 ms branch is not reachable in this build (`#ifndef ESPHOME_LOG_LEVEL` in the overlay allows an optional second build with `-DESPHOME_LOG_LEVEL=...` later).

### Pitfall 8: Uninitialised members
See Pattern 4 (zero-filled storage). Do not assert on uninitialised fields before `setup()`; `lastReconnectTimeMs` is first written by `sendFirstConnectionPacket()`/`reconnectUART()`.

### Pitfall 9: `ClimateCall::perform()` needs traits and a named entity
`make_call().set_mode(..).perform()` validates against `traits()`; configure supported modes/fan/swing like codegen, and register the entity name (`App.register_climate(&c,"clim",0,0)`), otherwise `perform()` rejects or logs. `set_name` does not exist in 2026.10 (compile error, verified).

### Pitfall 10: Per-test isolation
Reset in `SetUp()`: clock, log capture, in-memory preferences, `App`, the IDF-call recorder. Do not leave scheduler items from a previous test.

### Pitfall 11: FetchContent and offline
`FETCHCONTENT_SOURCE_DIR_ESPHOME=/path/to/esphome-2026.10.0` must keep working for offline dev; because the overlay approach needs no patch of the fetched tree this works without special handling.

## Code Examples

### Fake UART (works on all three variants; verified)
```cpp
// Source: experiment in scratchpad; ESPHome uart_component.h pure virtuals
struct FakeUart : esphome::uart::UARTComponent {
  std::vector<uint8_t> tx, rx;                       // captured writes / scripted reads
  void write_array(const uint8_t* d, size_t n) override { tx.insert(tx.end(), d, d + n); }
  bool peek_byte(uint8_t* d) override { if (rx.empty()) return false; *d = rx.front(); return true; }
  bool read_array(uint8_t* d, size_t n) override {
    if (rx.size() < n) return false; std::copy_n(rx.begin(), n, d); rx.erase(rx.begin(), rx.begin() + n); return true; }
  size_t available() override { return rx.size(); }
  esphome::uart::UARTFlushResult flush() override { return esphome::uart::UARTFlushResult::UART_FLUSH_RESULT_ASSUMED_SUCCESS; }
  void check_logger_conflict() override {}
#if defined(USE_ESP32) || defined(USE_ESP8266)
  void load_settings(bool) override {}
#endif
};
```

### Fixture skeleton and a first scenario (verified flow)
```cpp
class Cn105Fixture : public ::testing::Test {
 protected:
  void SetUp() override {
    TestClock::reset(0); TestLog::clear(); TestPrefs::clear();
    new (&esphome::App) esphome::Application();
    uart_.set_data_bits(8); uart_.set_parity(esphome::uart::UART_CONFIG_PARITY_EVEN);
    uart_.set_stop_bits(1); uart_.set_baud_rate(2400);
    std::memset(storage_, 0, sizeof storage_);                 // BUG-08: deterministic uninitialised members
    hp_ = new (storage_) esphome::CN105Climate(&uart_);
    esphome::App.register_climate(hp_, "clim", 0, 0);
    hp_->set_update_interval(1000);
    auto& t = hp_->config_traits();                            // mirror climate.py to_code
    for (auto m : {OFF, HEAT, COOL, DRY, FAN_ONLY, AUTO}) t.add_supported_mode(m);
    hp_->add_on_state_callback([this](esphome::climate::Climate& c) { publishes_.push_back(snapshot(c)); });
    hp_->setup();
  }
  void TearDown() override { hp_->~CN105Climate(); esphome::App.~Application(); }
  void step(uint32_t ms) { TestClock::advance(ms); hp_->loop(); esphome::App.scheduler.call(esphome::millis()); }
  void feed(std::vector<uint8_t> f) {                          // frames from test_real_frames.cpp, checksum appended
    f.push_back(cn105_protocol::checksum(f.data(), (int)f.size())); uart_.rx.insert(uart_.rx.end(), f.begin(), f.end()); }
  alignas(esphome::CN105Climate) unsigned char storage_[sizeof(esphome::CN105Climate)];
  FakeUart uart_; esphome::CN105Climate* hp_{}; std::vector<Published> publishes_;
};
// Verified observations: tx at t=10000 = FC 5A 01 30 02 CA 01 A8 (== CONNECT in cn105_types.h:40),
// after feed({0xFC,0x7A,0x01,0x30,0x01,0x00}) isHeatpumpConnected()==true, first poll frame
// FC 42 01 30 10 02 00.. 7B, then 0x03 (7A), 0x06 (77), 0x09 (74), 0x04 (79).
```

### BUG-01 reproduction on the unmodified production file (verified, LP64 run)
```cpp
cycleManagement c; g_ms = 0xFFFFFF00ull; c.init(); c.cycleStarted(); g_ms += 100; c.cycleEnded();
g_ms = 0x100000000ull + 0x800;                 // millis() == 2048, 2.3 s after the wrap
c.hasUpdateIntervalPassed(1000);               // -> false  (stall until millis() reaches ~0xFFFFFF00 again, 49.7 days)
c.cycleStarted() @0xFFFFFF00; clock -> wrap + 16 s; c.doesCycleTimeOut(1000);   // -> false (never times out)
```
Pin as `CycleManagement.Characterize_BUG01_StallsAfterMillisWrap` with `// Pins current buggy behaviour — flip in Phase 4 (TIME-01)`.

### BUG-02 reproduces on host
`hp_readings.cpp:84`: `float diff = abs(receivedStatus.roomTemperature - this->remoteTemperature_);` — `decltype(abs(0.3f))` is `int` in a TU that includes `cn105.h` (verified with a `static_assert`), so a 0.6 °C gap with a 0.4 °C margin yields `is_remote = true`. Cheap to pin in PR 1a/1b as `Characterize_BUG02_...` (flips in Phase 4 / TIME-04).

### gcovr commands (verified syntax, gcovr 8.6)
```bash
# per build dir (cn105 object libs live there)
gcovr -r "$REPO" --filter "$REPO/components/cn105/" --object-directory "$BUILD" \
      --exclude-directory '.*/CMakeFiles/.*' --exclude-throw-branches --exclude-unreachable-branches \
      --json "$OUT/tf_$NAME.json"
# merge + reports
gcovr -r "$REPO" --filter "$REPO/components/cn105/" -a "$OUT/tf_a.json" -a "$OUT/tf_b.json" \
      --markdown-summary "$OUT/summary.md" --cobertura "$OUT/coverage.xml" \
      --html-nested "$OUT/html/index.html" --json-summary "$OUT/summary.json"
```
`--root` must be the repo root so Cobertura file names are `components/cn105/...` (diff-cover joins them with `git diff`). `--json-summary` exposes exact `line_covered/line_total/branch_covered/branch_total`; compute percentages from the counts (the `*_percent` fields are rounded to 1 decimal).

### Ratchet gate (prototype, tested: 5 unit tests pass)
```python
TOLERANCE = 0.1
def pct(c, t): return 100.0 * c / t if t else 100.0
def evaluate(current, baseline, tolerance=TOLERANCE):
    status, res = "OK", {}
    for m in ("line", "branch"):
        cur = pct(current[m]["covered"], current[m]["total"]); delta = cur - baseline[m]
        if delta < -tolerance:                     res[m] = ("FAIL", cur, baseline[m], delta); status = "FAIL"
        elif delta > tolerance and status != "FAIL": res[m] = ("RATCHET", cur, baseline[m], delta); status = "RATCHET"
        else:                                      res[m] = ("OK", cur, baseline[m], delta)
    return status, res          # CLI: exit 1 only on FAIL; RATCHET prints the proposed JSON for the PR author
```
Self-test (`python3 -m unittest discover -s tests/unit/tools`; do not use `python3 -I -m unittest` — isolated mode cannot import the sibling module): drop beyond 0.1 -> FAIL; drop within 0.1 -> OK; rise -> RATCHET with proposed values; one metric falls while the other rises -> FAIL; equal -> OK. This satisfies D-14 ("both outcomes demonstrated") hermetically and permanently; the planner may add one documented throwaway PR as extra evidence.

### diff-cover + exemption (verified on a toy repo)
```bash
diff-cover coverage.xml --compare-branch="origin/${BASE_REF}" --format markdown:diff-cover.md   # report only
# gate in the script: parse "Coverage: NN%" (or use --json-report) and compare to 80 unless exempt
exempt=$(gh pr view "$PR" --json labels --jq '[.labels[].name]|index("coverage-exempt")!=null')
```
Reading the label **live via the API at run time** avoids adding `labeled`/`unlabeled` to the workflow's `pull_request` types (which would re-run the whole ESPHome matrix); the author adds the label and clicks "Re-run failed jobs". `actions/checkout` needs `fetch-depth: 0`.

### Sticky PR comment, no third-party action
```bash
marker='<!-- cn105-coverage-report -->'
{ echo "$marker"; cat "$OUT/summary.md"; echo; cat diff-cover.md; } > comment.md
id=$(gh api "repos/$REPO/issues/$PR/comments" --paginate --jq ".[] | select(.body|contains(\"$marker\")) | .id" | head -1)
if [ -n "$id" ]; then gh api -X PATCH "repos/$REPO/issues/comments/$id" -F body=@comment.md
else gh api -X POST "repos/$REPO/issues/$PR/comments" -F body=@comment.md; fi
```
`-F key=@file` reads the value from a file `[CITED: cli.github.com/manual/gh_api]`. Job needs `permissions: { contents: read, pull-requests: write }`; same-repo PRs only (fork PRs get a read-only token `[ASSUMED]`) so guard with `continue-on-error: true` and `if: github.event.pull_request.head.repo.full_name == github.repository`. The step summary is written unconditionally (`cat "$OUT/summary.md" >> "$GITHUB_STEP_SUMMARY"`).

### Recommended CI layout (satisfies "esphome-build still depends on unit tests")
- `unit-tests` (existing name, `runs-on: ubuntu-24.04`): `apt-get install g++-multilib`, run `tests/unit/coverage.sh --ci` (builds + runs all tests with coverage, writes `$OUT`), upload artifact (`if: always()`), write job summary. Fails only on build/test failure.
- `coverage-gate` (`needs: unit-tests`, download artifact): ratchet via `coverage_gate.py`, `diff-cover` (PR only), PR comment, label exemption. Fails on a gate violation.
- `esphome-build` keeps `needs: unit-tests` only, so a coverage-policy failure does not block the 7-config compile feedback, while remaining a separate required check. (Single-job alternative is acceptable; then a gate failure also skips the compile matrix.)
- On `push` to `main`: run the ratchet only (no diff, no comment). Pass PR number / base ref through `env:` (never interpolate `github.head_ref` or PR titles into `run:` scripts).

## Characterization scenario catalog (what to pin, with evidence)

All scenarios drive the real objects through `FakeUart` + `TestClock` + `App.scheduler.call`, and assert **bytes written** and **publishes** (D-15). Frames to *receive* come from `test_real_frames.cpp` (0x02 settings, 0x03 room temp, 0x06 status, 0x09 standby, 0x04 error) and `test_frame_parser.cpp` (`FC 7A 01 30 01 00 54` handshake reply) and `test_lossnay.cpp` (Lossnay 0x34 frames); checksums via `cn105_protocol::checksum`.

**`cycle_management` (struct, no climate needed; `Characterize_` tests with the clock set explicitly)**
- `init()` sets `cycleRunning=false`, `lastCompleteCycleMs=now`.
- `hasUpdateIntervalPassed` strict `>` boundary (`:58-59`); `doesCycleTimeOut` `> 2*interval+1000` (`:63-64`).
- `deferCycle()` writes a *future* absolute timestamp `now + 1500` at VERY_VERBOSE (`:26-35`); `cycleEnded()` only advances `lastCompleteCycleMs` if it is `< now` (`:47`), so a defer survives a cycle end.
- **BUG-01** `Characterize_BUG01_StallsAfterMillisWrap` (+ timeout never fires, + defer written just before wrap). **BUG-08** signed/unsigned: pin `long` vs `uint32_t` deltas under `-m32`.

**`request_scheduler` (constructible stand-alone with `std::function` callbacks; `context=nullptr` skips `canSend`)**
- Registration order, `send_next_after(0x00)` picks first enabled/`canSend`/interval-elapsed entry; `disabled`, `enable/disable_request`, `timer_bypass`; terminate callback when the list is exhausted; unknown code -> `process_response` returns false.
- Interval skip uses `last_request_time > 0` as "never sent" (a request sent at `millis()==0` looks unsent: quirk).
- Soft timeout: `soft_timeout_ms>0` schedules `timeout_callback_`; firing marks failure, disables at `maxFailures`, continues the chain.
- **BUG-07:** `soft_timeout_ms==0` (codes 0x02/0x03/0x04/0x06 in `registerInfoRequests`) -> no timer -> a missing response stalls the cycle until `doesCycleTimeOut`; `process_response(code)` for a code that is *not* awaited still runs `send_next_after` (duplicated chain) -> `Characterize_BUG07_LateResponseRestartsChain`, `..._ZeroSoftTimeoutStallsCycle`.

**`hp_writings` / `writePacket` / pending packet (`hp_writings.cpp:96-136` — read this session)**
- Connected write: bytes on the wire, `lastSend` updated. Not connected (`writePacket` else-branch): `reconnectUART()` (which itself writes CONNECT), copies the packet into `pending_packet_`, arms `set_timeout("write", 4000, ...)`; `try_write_pending_packet` retries every 2000 ms while the UART is not ready, then `writePacket(...)` and clears.
- **BUG-04:** a second `writePacket` while a user command is pending overwrites the single slot (`memcpy ... pending_packet_`) -> assert that only the info frame is transmitted after the 4 s retry; each failed write also triggers a fresh `reconnectUART()` CONNECT burst. Also: `length > PACKET_LEN` drops but reconnect already happened.
- **BUG-03:** `sendWantedSettingsDelegate` (`:228-248`) calls `writePacket`, then `publishWantedSettingsStateToHA()` and `wantedSettings.resetSettings()` unconditionally, even if the packet was only queued -> `Characterize_BUG03_PublishesAndClearsWantedAfterQueuedWrite`. Plus the observed double publish (control-time optimistic publish + send-time publish).
- `sendWantedSettings` throttle `CUSTOM_MILLIS - lastSend > 300` (`:257`); ACK `0x61` is accepted and ignored (`updateSuccess()` is a no-op): pin "0x61 changes nothing".
- Frame content for each info code (`createInfoPacket`) and for the SET packet; `sendFirstConnectionPacket` installer mode 0x5B vs 0x5A and the 10 s `checkFirstConnection` fallback.

**`climateControls` (command -> wantedSettings -> publish)**
- ESP32: `std::lock_guard` path; HOST/ESP8266: `emulateMutex` path incl. the retry schedule (100 ms, x1.2, forced unlock after 10 retries) — set `wantedSettingsMutex=true` (private; reachable via `-fno-access-control`).
- `processModeChange/TemperatureChange/FanChange/SwingChange` -> `wantedSettings.{mode,power,temperature,fan,...}`, `hasChanged=true`, `hasBeenSent=false`, `lastChange=now` (`finalizeControlIfUpdated`), immediate `publish_state()` (optimistic).
- `checkPendingWantedSettings` debounce (`debounce_delay_`) and the `hasBeenSent` short-circuit; `save_setpoint_state_` via in-memory prefs.

**Profile state handling**
- HeatPump: `decode_settings`/`apply_received_settings` guards (`wantedSettings.*==nullptr` protects user demands), grace logic `isWithinPostSendGrace` (`update_interval_ + DEFER_SCHEDULE_UPDATE_LOOP_DELAY` against absolute `lastChange`), `shouldApplyIncomingSetpoint`, vane grace, `control_mode` mapping (HEAT_COOL -> hardware AUTO), single vs dual setpoint paths, `update_action`.
- Lossnay (`LossnayProfile`): `allow_operation` always logs a warning and returns false, temperature changes ignored, `mode_setting`/`fan_setting` lookups, `decode_settings` via captured 0x34 frames.
- **BUG-05 (optional but cheap on the ESP32 variant):** `reconnectUART()` -> recorded IDF calls show `UART_NUM_0` and pins 1/2 when `uart_port_==-1`/pins unset.

**Observation rule for unreachable-in-harness branches:** WAIT_WIFI (`componentEntries.cpp:133,147`) is reachable only with the overlay `USE_WIFI` + `wifi::global_wifi_component` stub (verified to compile); include it in 1a or 1b.

### Real-capture flags (CONTEXT: "flag explicitly rather than inventing frames")
- **PR 1a needs no new capture.** Everything it pins is covered by the frames already in `tests/unit/` (0x7A reply, 0x62 replies 0x02/0x03/0x06/0x09/0x04, the Lossnay 0x34 frames) or by frames the production code generates itself.
- **Synthetic, label them as such in the test comment:** the `0x61` ACK frame and the `0x7B` installer-handshake reply (no capture exists in the repo; the production code ignores the ACK payload and only checks `command` / `raw()[3]` for the handshake, so a checksum-valid synthetic frame does not change the pinned behaviour).
- **Capture needed before PR 1b can assert real decoding (ask the user):** (1) `0x42` HVAC-options reply (air purifier / night mode / circulator switches) from a unit that supports it; (2) `0x20` / `0x22` functions replies for the installer-mode hardware-settings selects (only negative-length cases exist in `test_profiles.cpp`); (3) a real `0x61` ACK following a `0x41` SET and following a `0x07` remote-temperature packet (also valuable for Phase 6 CMD-02); (4) a DEFROST sub-mode `0x09` reply (only an image, `captures/capture_stage and submode.png`, exists). Until then 1b should limit itself to paths exercised by existing frames and mark any synthetic frame.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Hand-written ESPHome stubs (`tests/unit/mocks`) | Real ESPHome core compiled on host with overlay `defines.h` | this phase (D-01) | Exact `Scheduler`, `Climate`, entity behaviour; cost = shims confined to `harness/` |
| `esphome.h`/`defines.h`/`version.h` generated per build | Hand-written overlay, force-included | ESPHome tree ships a dummy `defines.h` for static analysis only | Must track ESPHome internals when bumping (`ESPHOME_COMPONENT_COUNT`, `ESPHOME_ENTITY_*_COUNT`, `USE_*`) |
| ESPHome <= C++17 | `-std=gnu++20` required (`std::span`, concepts in `helpers.h`) | ESPHome 2026.x host sets `gnu++20` | Harness targets use C++20 while the existing pure tests stay C++17 |
| `ubuntu-latest` = 24.04 | `ubuntu-latest` -> 26.04 in Nov 2026 | announced in runner-images README | Pin `ubuntu-24.04` to keep coverage numbers stable |

**Deprecated/outdated:** the minimum-version (2025.7 - 2025.10) `ClimateTraits` legacy API cannot be exercised under the 2026.10.0 headers; `cn105_legacy_action_tests` keeps covering the old `DEFROSTING`-less enum via mocks, and the 7 CI configs cover compilation (D-02).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `gh` CLI is preinstalled on `ubuntu-24.04` runners | Supporting tools / CI | Add an install step (low impact) |
| A2 | Fork-originated PRs get a read-only `GITHUB_TOKEN`, so the sticky comment would fail; PRs here are same-repo | PR comment | Comment step silently skipped for fork PRs (guarded with `continue-on-error`) |
| A3 | `$GITHUB_STEP_SUMMARY` accepts the gcovr Markdown summary size (limit is about 1 MiB) | CI layout | Truncated summary; artifact still has everything |
| A4 | The `ubuntu-24.04` runner's GCC is the same 13.x minor as `ubuntu:24.04` Docker (13.3.0) so `--docker` reproduces CI exactly | Pitfall 2 | Small residual drift; first CI run defines the baseline anyway |
| A5 | Real firmware builds always define `USE_WIFI` (all examples configure `wifi:`) | Pattern 1 | If a user builds without wifi, the non-WIFI branch is what runs; characterized path differs only in `maybe_start_connection_` |
| A6 | A synthetic checksum-valid `0x61` / `0x7B` frame is behaviourally equivalent to a real one for the code paths pinned in 1a | Real-capture flags | If `processCommand`/profile ever inspects ACK payload bytes, the pin would be incomplete (Phase 6 will need real ACKs) |
| A7 | `GITHUB_TOKEN` can be given `pull-requests: write` at job level for `issues` comment endpoints | CI layout | Use `issues: write` additionally (comments are issue comments) — verify on the first PR |

## Open Questions

1. **D-04 literal reading: is a true `USE_ESP8266` shim variant worth its fidelity cost?**
   - What we know: it compiles, links and runs (6 headers, ~15 stubs); `cn105` code only distinguishes `USE_ESP32` vs not, so HOST already executes the same cn105 paths as ESP8266.
   - What's unclear: whether the user wants the extra ESPHome-level ESP8266 surface (`ESPHOME_THREAD_SINGLE`, `Millis64Impl`) in the loop.
   - Recommendation: implement ESP32 + ESP8266 exactly as D-04 says; keep HOST as an optional third smoke build (zero shim) only if cheap.
2. **Adopt `-m32` for the characterization build?** Recommended (Pitfall 1). It adds `g++-multilib` to CI and local Docker; it deviates from nothing in CONTEXT but changes the fidelity story, so confirm with the user.
3. **Seam choice:** `-fno-access-control` on test TUs (no production edit, GCC-only) vs one-word `private:` -> `protected:` at `cn105.h:391` (D-03-sanctioned fallback). Recommend the flag; decide before writing the fixture.
4. **CI job layout:** two jobs (`unit-tests` + `coverage-gate`) vs one. Recommended: two.
5. **First baseline value:** must come from a CI run on `ubuntu-24.04`; sequence inside PR 1a: land tooling + harness smoke -> take baseline v0 from CI -> add characterization suites -> ratchet the committed baseline before opening review.
6. **`ESPHOME_LOG_LEVEL`:** VERY_VERBOSE (max instrumented lines, deferral 1500 ms) vs DEBUG. Recommendation above; confirm.
7. **Do existing pure tests also run in the 32-bit build?** They pass under `-m32` (verified); running them inside the same coverage build simplifies `coverage.sh` (one build dir) but keep the original 64-bit job path untouched as the fast gate.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| g++ / gcov | build, coverage | yes | 15.2.0 local; 13.3.0 in `ubuntu:24.04` | — |
| g++-multilib (`-m32`) | ILP32 fidelity | no locally (works in Docker) | 13.3 in container | `coverage.sh --docker`; CI installs via apt |
| CMake | build | yes | 4.2.3 local; 3.31.6 runner | — |
| Python 3 | gate, gcovr | yes | 3.14.4 local; 3.12.3 runner | — |
| gcovr / diff-cover | reports | no (installed in a scratch venv) | 8.6 / 10.6.0 pinned | `pip install -r requirements-coverage.txt` |
| Docker | `--docker` parity | yes | 29.9.2 | native run (informational numbers) |
| Network (GitHub, PyPI) | FetchContent esphome/googletest, pip | yes | — | `FETCHCONTENT_SOURCE_DIR_ESPHOME` / `..._GOOGLETEST` for offline |
| `gh` CLI | local dry-run of comment script | yes | 2.46.0 | — |
| ESP boards / real CN105 captures | PR 1b only | no | — | see Real-capture flags |

**Missing dependencies with no fallback:** none for PR 1a.
**Missing dependencies with fallback:** `g++-multilib` locally (use Docker).

## Validation Architecture

> `workflow.nyquist_validation` is `true` in `.planning/config.json`.

### Test Framework
| Property | Value |
|----------|-------|
| Framework | GoogleTest v1.15.2 (+ `TEST_F` fixture introduced in this phase); Python `unittest` for the gate |
| Config file | `tests/unit/CMakeLists.txt` (+ new `tests/unit/harness/CMakeLists.txt`) |
| Quick run command | `ctest --test-dir tests/unit/build-cov -R 'Characterization|CycleManagement|RequestScheduler' --output-on-failure` (or a `--gtest_filter` on the per-variant binary) |
| Full suite command | `tests/unit/coverage.sh` (builds, runs every test configuration, writes reports); CI: `tests/unit/coverage.sh --ci` |
| Gate self-test | `python3 -m unittest discover -s tests/unit/tools -v` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| COV-01 | Line + branch coverage of `components/cn105/` published (summary + artifact); one local command | CI integration + script smoke | `tests/unit/coverage.sh && test -s out/summary.json && test -s out/html/index.html` | Wave 0 |
| COV-01 | Every `components/cn105/*.cpp` is in the report (D-10) | script check | `tests/unit/coverage.sh --check-scope` | Wave 0 |
| COV-02 | Drop > 0.1 pt fails, rise proposes ratchet | unit (Python) | `python3 -m unittest discover -s tests/unit/tools` | Wave 0 |
| COV-02 | Baseline file present and parsable, matches tool versions | script check | `python3 tests/unit/tools/coverage_gate.py --summary out/summary.json --baseline tests/unit/coverage-baseline.json` | Wave 0 |
| COV-03 | Changed-line coverage reported, 80 % blocking, label bypass | CI integration | PR run of `coverage-gate` job (evidence in the PR; local: `diff-cover out/coverage.xml --compare-branch=origin/main`) | Wave 0 |
| COV-04 | Characterization of cycle_management | unit (gtest) | `<bin> --gtest_filter='CycleManagement.*'` | Wave 0 |
| COV-04 | Characterization of request_scheduler | unit (gtest) | `<bin> --gtest_filter='RequestScheduler.*'` | Wave 0 |
| COV-04 | Characterization of hp_writings/writePacket/pending packet | unit (gtest) | `<bin> --gtest_filter='HpWritings.*'` | Wave 0 |
| COV-04 | Characterization of climateControls (ESP32 + ESP8266 paths) | unit (gtest, 2 variants) | `<bin_esp32> / <bin_esp8266> --gtest_filter='ClimateControls.*'` | Wave 0 |
| COV-04 | Characterization of profile state (HeatPump + Lossnay) | unit (gtest) | `<bin> --gtest_filter='ProfileState.*'` | Wave 0 |
| Gate 5 | Existing 287 tests still pass; 7 ESPHome configs compile; no shipped behaviour change | regression | `ctest` full + `git diff --stat origin/main -- components/` (expect empty or seam-only) + CI `esphome-build` | existing |

### Sampling Rate
- **Per task commit:** the affected module's gtest filter on the ESP32 variant (seconds) or `python3 -m unittest` for the gate.
- **Per wave merge:** full `tests/unit/coverage.sh` (all variants, ~1-2 min) and gate self-test.
- **Phase gate:** CI green on the PR: `unit-tests`, `coverage-gate` (ratchet OK or proposed update committed), `esphome-build` 7/7; `grep -rn Characterize_BUG tests/unit/characterization` lists BUG-01/03/04/07 (+02/05/08 if added).

### Wave 0 Gaps
- [ ] `tests/unit/harness/` (FetchContent, overlay, shims, platform stubs) — covers COV-04 prerequisite
- [ ] `tests/unit/characterization/fixture.{h,cpp}` + smoke test (CONNECT bytes, handshake, one poll cycle) on ESP32 + ESP8266
- [ ] `tests/unit/coverage.sh`, `requirements-coverage.txt`, `tools/coverage_gate.py` + `test_coverage_gate.py`, `coverage-baseline.json`
- [ ] `.github/workflows/build.yaml`: `ubuntu-24.04`, multilib, coverage + gate jobs, artifact, comment
- [ ] `.gitignore`: `tests/unit/build-cov*/`, `tests/unit/out/`
- [ ] `tests/unit/README.md` (+ short README pointer) documenting the local command, the ratchet procedure and the ESPHome bump procedure

## Security Domain

> `security_enforcement` is enabled (ASVS level 1, block on high). This phase touches CI and supply chain only; no runtime/shipped code.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | — |
| V3 Session Management | no | — |
| V4 Access Control | yes (CI token scope) | job-level `permissions:` least privilege (`contents: read`; `pull-requests: write` only on `coverage-gate`) |
| V5 Input Validation | yes (CI) | pass PR number/base ref via `env:`; never interpolate `github.head_ref`/titles into `run:`; comment body is built only from tool output files |
| V6 Cryptography | no | — |
| V14 Configuration / supply chain | yes | pinned versions (ESPHome tag/commit, gcovr 8.6, diff-cover 10.6.0, GoogleTest tag); no third-party Actions; no secret needed (D-06) |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Compromised or typosquatted Python package (gcovr / diff-cover) | Tampering | pin exact versions in `requirements-coverage.txt`; human verification checkpoint before first install (seam verdict SUS) |
| Script injection via PR metadata in workflow `run:` | Tampering / EoP | use `env:` indirection, no `pull_request_target`, quote variables |
| Over-privileged `GITHUB_TOKEN` | EoP | per-job `permissions`; comment step only on same-repo PRs |
| Mutable upstream tag (ESPHome) | Tampering | immutable release tag `2026.10.0`; optionally assert `git rev-parse HEAD == 704551a6...` after FetchContent |
| Running untrusted PR code with write token | EoP | `pull_request` (not `_target`); fork PRs get a read-only token `[ASSUMED]` |

## Sources

### Primary (HIGH confidence)
- ESPHome 2026.10.0 source tree (`git clone --depth 1 --branch 2026.10.0`, commit 704551a6...): `esphome/core/{defines,version,hal,application,entity_types,millis_internal,wake,log,helpers}.h`, `core/log.cpp`, `components/{host,esp32,esp8266,uart,climate}/...` — read and compiled.
- GitHub API: `esphome/esphome` release 2026.10.0 (published 2026-10-21, `immutable: true`); Docker Hub `esphome/esphome:stable` last pushed 2026-10-21; `actions/*` latest releases.
- PyPI JSON for gcovr and diff-cover (release dates, project URLs).
- Experiments in `.../scratchpad/exp` (host, ESP32-shim, ESP8266-shim builds; Docker `ubuntu:24.04` g++ 13.3 with `-m32`; gcovr 8.6 + diff-cover 10.6.0 runs; gate prototype).
- Repository files read this session: `components/cn105/{cn105_types.h,cn105.h,cycle_management.cpp,Globals.h,hp_writings.cpp,hp_readings.cpp}`; others viewed with `cat/sed` during analysis.
- `https://cli.github.com/manual/gh_api` — `-F key=@file` semantics.

### Secondary (MEDIUM confidence)
- `actions/runner-images` README (ubuntu-latest -> 26.04 in Nov 2026; Ubuntu 24.04 toolset: g++ 4:13.2.0-7ubuntu1, Python 3.12.3, CMake 3.31.6).

### Tertiary (LOW confidence)
- GITHUB_TOKEN fork-PR permissions, `gh` availability on runners, step-summary size limit (training knowledge; see Assumptions A1-A3, A7).

## Metadata

**Confidence breakdown:**
- Standard stack / harness feasibility: HIGH — built and run on three variants, 64- and 32-bit, two GCC versions.
- Coverage tooling and gates: HIGH — gcovr/diff-cover behaviour and gate logic exercised locally; CI-runner specifics (gh, token scopes) MEDIUM.
- Architecture / scenario catalog: HIGH for BUG-01/02/03/04/07 mechanics (code read, BUG-01/02 reproduced), MEDIUM for the profile-state scenario list (derived from reading, not all executed).
- Pitfalls: HIGH (each reproduced or measured).

**Research date:** 2026-10-27
**Valid until:** 2026-11-26 for tool versions and runner behaviour (ubuntu-latest switch in Nov 2026); the harness recipe is valid for ESPHome 2026.10.0 only and must be re-verified on every ESPHome bump.
