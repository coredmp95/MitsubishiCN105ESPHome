# Phase 1: Coverage Safety Net - Context

**Gathered:** 2026-10-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Every future change to `components/cn105/` is measured against a recorded coverage baseline (lines + branches), each PR shows and gates the coverage of its changed lines, and the code the milestone will change has its current behaviour — known bugs included — pinned by characterization tests before it is touched. Requirements: COV-01, COV-02, COV-03, COV-04. No behaviour change to the shipped component.

Delivered as **two PRs** (see D-17): 1a = coverage tooling + harness + characterization of the milestone's target paths (COV-01..04 complete); 1b = characterization extended to the rest of the component.

</domain>

<decisions>
## Implementation Decisions

### Test harness (compiling `CN105Climate` under GoogleTest)
- **D-01:** Use the **real ESPHome C++ core/headers**, fetched at a pinned version (e.g. CMake `FetchContent` of the esphome repo), compiled for the ESPHome `host` platform with a hand-written `esphome/core/defines.h` replacing codegen output. Not hand-written stubs. The existing `tests/unit/mocks/` stays for the current pure tests; the planner decides whether they migrate. — **Reversibility:** costly — every characterization test and the Phase 2 simulator/Phase 3 host job build on this harness; switching to stubs later means re-plumbing all of them.
- **D-02:** Pin the **exact ESPHome version CI `stable` uses today** (`ghcr.io/esphome/esphome:stable` = **2026.10.0** on 2026-10-27), bumped manually alongside the CI. Minimum-version (2025.7) paths stay covered only by CI config compilation.
- **D-03:** Production code changes in this phase are limited to **behaviour-neutral testability seams** (e.g. widening access, making a method virtual, splitting an include). **No `#ifdef UNIT_TEST` / test-only conditionals in shipped code.** Anything else is done on the test side (stubs at link level, `esphome::millis()` test clock, fake UART).
- **D-04:** Build **two test configurations: `USE_ESP32` and `USE_ESP8266`**, so the platform-specific branches (`std::mutex` vs `emulateMutex`, FreeRTOS includes) are both covered — Phase 7 removes the mutex and needs both sides pinned. Note for research: the ESPHome `host` platform normally defines `USE_HOST`, so the ESP32/ESP8266 variants need their platform defines and minimal FreeRTOS/ESP8266 shims on host; research must confirm feasibility and propose the shim surface.
- **D-05:** Time is driven through a test-side stub of `esphome::millis()` (what `CUSTOM_MILLIS` expands to in `components/cn105/Globals.h`), controllable by tests, including values near `UINT32_MAX`. The production clock abstraction is **not** introduced here (that is Phase 4, TIME-01/DEBT-07).

### Coverage tooling and reporting
- **D-06:** **In-house only** — no Codecov/Coveralls, no third-party upload, no token. GCC `--coverage` + **gcovr**; Markdown summary written to `$GITHUB_STEP_SUMMARY`, HTML report uploaded as a workflow artifact, Cobertura XML produced for diff coverage.
- **D-07:** Diff coverage via **diff-cover** on the Cobertura XML against the PR base; result shown in the job summary **and** in a single PR comment that is updated in place (posted with the repository `GITHUB_TOKEN`, `pull-requests: write`). Repo is the user's fork `coredmp95/MitsubishiCN105ESPHome`.
- **D-08:** One local entry point: a script (e.g. `tests/unit/coverage.sh`) that configures a coverage build, runs all test configurations, and produces the same summary/HTML/XML as CI. **CI calls the same script**, so local and CI results match. Documented in `tests/unit/` README or the main README.
- **D-09:** Toolchain: **GCC + gcovr** (what `ubuntu-latest` provides). Clang/llvm-cov not used in this phase.

### Baseline and gates (ratchet)
- **D-10:** Coverage scope = **all `.cpp`/`.h` under `components/cn105/`**; files never compiled into tests count as 0% (gcovr root/filter configured so they appear). Excluded: `climate.py`, `__init__.py`, anything under `tests/`. The initial baseline is expected to be low; that is accepted.
- **D-11:** Gate on **both line and branch coverage**, each with its own baseline value.
- **D-12:** Baseline stored in a **committed file `tests/unit/coverage-baseline.json`** (line %, branch %). CI fails if either metric drops by **more than 0.1 point** below the baseline. When coverage rises, CI reports the proposed new values and the PR author commits the updated baseline in the same PR (reviewed like code). No CI auto-commit to `main`.
- **D-13:** Diff coverage is **blocking at 80% of changed lines** (lines in `components/cn105/`). Exception mechanism: a **`coverage-exempt` PR label**, with the justification written in the PR description; the gate then reports but does not fail.
- **D-14:** Success criterion 2 requires demonstrating both outcomes once (a drop fails, a rise allows ratcheting) — e.g. a scripted self-test of the gate logic or a documented throwaway PR; planner chooses.

### Characterization tests
- **D-15:** Assert **both the exact bytes written to the (fake) UART and the state published to HA** (climate mode/action, target/current temperatures, fan, swing/vanes, and the auxiliary entities touched) for each scenario.
- **D-16:** Tests that pin a known bug are **named after the audit ID and marked with a comment**, e.g. `CycleManagement.Characterize_BUG01_StallsAfterMillisWrap` with `// Pins current buggy behaviour — flip in Phase 4 (TIME-01)`. They pass today; the phase that fixes the bug flips them in the same PR. Greppable by `Characterize_BUG`. Pinned quirks at minimum: BUG-01 (absolute-timestamp comparisons / wrap stall), BUG-03 (optimistic publish + `wantedSettings` cleared after a merely queued write), BUG-04 (single `pending_packet_` slot overwritten by info requests), BUG-07 (`soft_timeout_ms = 0` stall, late response restarting the chain).
- **D-17:** Depth = **as much as possible, prioritized, in two PRs**:
  - **PR 1a (completes COV-01..04):** coverage tooling + gates + harness + characterization of the milestone's target paths: `cycle_management`, `request_scheduler`, `hp_writings` / `writePacket` / pending packet, `climateControls` (command → wantedSettings → publish), and state handling in the profiles (`heatpump_profile`, `lossnay_profile`).
  - **PR 1b (extension):** sensors, selects, switches, installer/hardware settings functions, Lossnay specifics, remote temperature path, remaining decode paths — raising the baseline.
  - Phase 2 may start once PR 1a is merged.
- **D-18:** Layout: **one file per module under `tests/unit/characterization/`** (`test_cycle_management.cpp`, `test_request_scheduler.cpp`, `test_hp_writings.cpp`, `test_climate_controls.cpp`, `test_profile_state.cpp`, …) sharing a **common `TEST_F` fixture** that builds a `CN105Climate` with a fake UART and the test clock. The fixture is designed for reuse by the Phase 2 simulator (SIM-02).

### Post-research decisions (2026-10-27, answers to 01-RESEARCH.md open questions)
- **D-19:** Characterization harness variants are **`USE_ESP32` + `USE_ESP8266`** (D-04 as written). No separate `USE_HOST` variant.
- **D-20:** The harness is compiled **`-m32`** (ILP32, so `unsigned long` is 4 bytes as on ESP). CI installs `g++-multilib` and pins `runs-on: ubuntu-24.04`. The coverage baseline is taken from the first CI run, not from a local machine.
- **D-21:** Tests reach `CN105Climate` private members with **`-fno-access-control` on test translation units only**. No change to `components/cn105/cn105.h`.
- **D-22:** Harness log level is **`ESPHOME_LOG_LEVEL_VERY_VERBOSE`** (`deferCycle()` = 1500 ms is pinned under this level).

### Plan-review decisions (2026-10-27, answers to plan-checker warnings)
- **D-23:** Plan 01-06 (PR 1a) **keeps two synthetic CN105 frames**: the `0x61` ACK (`FC 61 01 30 10` followed by 16 zero bytes) and the `0x7B` installer-mode connect reply (`FC 7B 01 30 01 00`). Both are checksum-valid and labelled `SYNTHETIC` in code comments, because no real capture exists yet. They are replaced by real captures once the user can record them (capture requests go through plan 01-10 and the "Wanted captures" list). The user accepted this explicitly on 2026-10-27, in answer to the plan-checker warning on 01-06; it is a recorded exception to the "flag, don't invent" rule in `<specifics>` for these two frames.

### Claude's Discretion
- Exact FetchContent mechanics, the content of the hand-written `defines.h`, and how ESPHome core sources are selected for the host build.
- Fake UART design (byte capture + scripted RX) and how HA-published state is observed (real ESPHome entity objects vs test listeners).
- gcovr options (exclusions, branch noise filtering such as `--exclude-throw-branches` / `--exclude-unreachable-branches`) as long as D-10/D-11 hold.
- CI job layout (extend the existing `unit-tests` job vs a separate `coverage` job), as long as the existing `esphome-build` matrix still depends on unit tests.
- Whether existing pure tests also run under the new harness or keep their current lightweight binary.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project scope and rules
- `.planning/ROADMAP.md` — Phase 1 goal, success criteria and Planning Notes (clock seam, harness difficulty)
- `.planning/REQUIREMENTS.md` — COV-01..04 and the cross-cutting acceptance rules
- `.planning/PROJECT.md` — constraints (no breaking changes, one PR per phase on the fork, coverage constraint)
- `.planning/AUDIT-CN105-2026-10-27.md` §3 — bug causes and locations to pin (BUG-01, BUG-03, BUG-04, BUG-07, BUG-08)

### Codebase maps
- `.planning/codebase/TESTING.md` — current GoogleTest setup, run commands, mocks, known gaps (`test_checksum.cpp` reimplements `checkSum()`)
- `.planning/codebase/ARCHITECTURE.md` — data flow (poll path, command path, connection lifecycle) to drive characterization scenarios
- `.planning/codebase/STRUCTURE.md` — file layout of `components/cn105/`
- `.planning/codebase/CONCERNS.md` — fragile areas and test coverage gaps

### Code
- `tests/unit/CMakeLists.txt` — current build (only `heatpump_functions_protocol.cpp` from production is compiled; `CN105_SRC_DIR`)
- `tests/unit/mocks/` — existing ESPHome stubs (`esphome_stubs.h`, `esphome/core/version.h`, climate enums)
- `components/cn105/Globals.h` — `CUSTOM_MILLIS` → `esphome::millis()` (time seam)
- `components/cn105/cn105.h` — `CN105Climate` (≈60 public fields, includes `<esphome.h>`, uart, sensors, selects…)
- `components/cn105/esphome_compat.h` — version shims the pinned headers must satisfy
- `.github/workflows/build.yaml` — `unit-tests` job and the 7-config `esphome-build` matrix on `ghcr.io/esphome/esphome:stable`
- `tests/unit/test_real_frames.cpp` — real captured frames usable as characterization inputs

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `tests/unit/test_real_frames.cpp`: real frames (connect, settings in several modes) — reuse as scripted RX data in characterization scenarios.
- `cn105_protocol.h` / `frame_parser.h` / `*_protocol.h`: pure, already-tested protocol helpers — characterization can use them to build and check frames rather than hard-coding everything.
- `tests/unit/mocks/esphome_stubs.h`: existing ESP_LOG no-op approach — superseded for the new harness by real ESPHome logging, but keeps the pure tests light.

### Established Patterns
- GoogleTest via CMake FetchContent (v1.15.2), `gtest_discover_tests`, CTest; two binaries today (`cn105_tests`, `cn105_legacy_action_tests` for an older ESPHome version code). New test configurations follow the same CMake/CTest pattern.
- Test naming: `tests/unit/test_<topic>.cpp`, PascalCase suites; no `TEST_F` yet — the characterization fixture introduces the first one.
- CI: `unit-tests` job runs configure/build/ctest on `ubuntu-latest`; `esphome-build` matrix `needs: unit-tests`.

### Integration Points
- New CMake targets/configurations in `tests/unit/CMakeLists.txt` (harness + ESP32/ESP8266 variants + coverage flags).
- New script `tests/unit/coverage.sh`, new file `tests/unit/coverage-baseline.json`.
- `.github/workflows/build.yaml`: coverage step, summary, artifact, diff-cover, PR comment, `coverage-exempt` label check; `permissions: pull-requests: write`.
- Possible behaviour-neutral seams in `components/cn105/` (only if needed, D-03).

</code_context>

<specifics>
## Specific Ideas

- The user's goal is confidence that nothing breaks ("une assurance… pour être relativement sûr de ne rien casser") — prefer pinning more behaviour over less, and make regressions loud.
- Bug-pinning tests must be easy to find and flip: `Characterize_BUG<NN>_…` naming plus a comment naming the fixing phase/requirement.
- Real CN105 captures between the official module and the indoor unit are not available yet (user lacks the Y-cable hardware; sniffer tooling is on PR #1 of the fork). If a characterization scenario genuinely needs real frames beyond `test_real_frames.cpp`, **flag it to the user explicitly with the scenario to capture** rather than inventing frames.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 01-coverage-safety-net*
*Context gathered: 2026-10-27*
