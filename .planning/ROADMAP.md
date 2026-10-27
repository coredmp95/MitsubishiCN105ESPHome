# Roadmap: MitsubishiCN105ESPHome

## Overview

This milestone (Reliability & Architecture) fixes the bugs found in the 2026-10-27 audit and then refactors the component's internals, without changing anything users configure. The order is deliberate: first a safety net (measured coverage, characterization tests, and a software CN105 heat pump simulator that drives the real `CN105Climate` on the host, plus an ESPHome `host` end-to-end job), then the audit fixes by severity (critical timing, link, commands + diagnostics), then cleanup and CI hardening, and last the typing and encapsulation refactor, which is safe to do only because everything before it pins the behaviour. Each phase is one PR to `main` and is releasable on its own. Audit P6 features (presets, real HEAT_COOL range, kWh wrap, etc.) are deferred to the next milestone.

**Cross-cutting acceptance rules (apply to every phase; restated in each phase's last success criterion):**
- All pre-existing unit tests pass, and each fixed bug has a new unit test
- Coverage of `components/cn105/` is not below the recorded baseline, and changed lines are covered (diff coverage reviewed in the PR)
- All 7 CI ESPHome configs (ESP32 + ESP8266) compile
- No change to existing YAML keys or HA entity IDs (any rename goes through a deprecation warning)
- User-visible additions are documented in the README

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [ ] **Phase 1: Coverage Safety Net** - Measured coverage with a ratcheting baseline and diff coverage, plus characterization tests pinning the code the milestone will change
- [ ] **Phase 2: Heat Pump Simulator** - Software CN105 unit with fault injection, real-frame replay and simulated time, driving the real `CN105Climate` in GoogleTest
- [ ] **Phase 3: ESPHome Host End-to-End Job** - CI job that compiles for the ESPHome `host` platform and runs against the simulator over a pty
- [ ] **Phase 4: Critical Timing Fixes** - Polling survives `millis()` wraparound, remote-temperature margin is honoured, timestamps and fields are well-defined
- [ ] **Phase 5: Link Reliability** - Single debounced reconnect, commands never displaced, no stalled poll cycle, correct UART reinit target, heat pump unavailability visible in HA
- [ ] **Phase 6: Command Reliability & Diagnostics** - Commands are confirmed, retried and honestly reported; link diagnostics and defrost sensors exposed
- [ ] **Phase 7: Cleanup & CI Hardening** - Mutex, dead code and mojibake removed; ESPHome `beta` build, `-Werror` and ASan/UBSan in CI
- [ ] **Phase 8: Strong Typing & Encapsulation** - `enum class` states and `CN105Climate` internals hidden behind the protocol profiles

## Phase Details

### Phase 1: Coverage Safety Net
**Goal**: Every future change to `components/cn105/` is measured against a recorded coverage baseline, and the code this milestone will change has its current behaviour pinned by tests before it is touched.
**Depends on**: Nothing (first phase)
**Requirements**: COV-01, COV-02, COV-03, COV-04
**Success Criteria** (what must be TRUE):
  1. Every CI run publishes line and branch coverage of `components/cn105/` computed from the unit tests (job summary plus downloadable artifact), and a developer can produce the same report locally with one documented command.
  2. A coverage baseline is recorded, and a change that lowers total coverage below it fails CI; a change that raises it lets the baseline be ratcheted up (both outcomes demonstrated once).
  3. Each PR shows the coverage of its changed lines, so an untested modification is visible in review without opening the full report.
  4. Characterization tests for `cycle_management`, `request_scheduler`, `hp_writings`/`writePacket`, `climateControls` and the state handling in the profiles pass against the unmodified production code, now compiled into the unit-test build; they pin today's behaviour, known quirks included (absolute-timestamp comparisons, optimistic publish, single pending-packet slot), which later phases flip knowingly.
  5. Phase gate: all pre-existing unit tests still pass, the 7 CI ESPHome configs still compile, and the shipped component's behaviour is unchanged (production edits limited to behaviour-preserving testability seams, if any).
**Plans:** 15 plans (PR 1a: 01-01..01-11, PR 1b: 01-12..01-15)

Plans:
- [ ] 01-01-PLAN.md — [1a] Host harness tracer: real ESPHome 2026.10.0 core + unmodified cn105 under GoogleTest, ESP32 -m32 variant, test clock and platform control API, fixture base, Docker dev image
- [ ] 01-02-PLAN.md — [1a] Harness fixture completion (handshake, poll cycle, command, publishes, real frames), run-harness.sh, CN105_COVERAGE + coverage-only header TUs
- [ ] 01-03-PLAN.md — [1a] Harness ESP8266 -m32 variant: shims, stubs, smoke suite and coverage instrumentation on both variants
- [ ] 01-04-PLAN.md — [1a] Coverage gate logic in stdlib Python (ratchet, propose, scope, diff 80%, sticky-comment target) with unittest self-test
- [ ] 01-05-PLAN.md — [1a] tests/unit/coverage.sh single entry point (gcovr reports, scope check, --docker/--ci/--gate), pinned tools after legitimacy checkpoint, provisional baseline
- [ ] 01-06-PLAN.md — [1a] CI: unit-tests on ubuntu-24.04 with coverage summary/artifact, coverage-gate job (ratchet, diff-cover, label exemption, sticky comment), docs
- [ ] 01-07-PLAN.md — [1a] Characterization: cycle_management + request_scheduler (BUG-01, BUG-07)
- [ ] 01-08-PLAN.md — [1a] Characterization: hp_writings / writePacket / pending packet (BUG-03, BUG-04)
- [ ] 01-09-PLAN.md — [1a] Characterization: climateControls command path and platform locking (BUG-03, BUG-08)
- [ ] 01-10-PLAN.md — [1a] Characterization: profile state handling (HeatPump + Lossnay)
- [ ] 01-11-PLAN.md — [1a] PR 1a on the fork: CI-derived baseline, live gate demonstration (D-13/D-14)
- [ ] 01-12-PLAN.md — [1b] Remote-temperature path (BUG-02) + real-capture decision checkpoint
- [ ] 01-13-PLAN.md — [1b] Connection lifecycle and read/decode paths (BUG-05, BUG-08)
- [ ] 01-14-PLAN.md — [1b] Entities, installer hardware settings, Lossnay specifics
- [ ] 01-15-PLAN.md — [1b] PR 1b on the fork: ratcheted CI baseline, phase gate

### Phase 2: Heat Pump Simulator
**Goal**: Developers can run the real `CN105Climate` against a simulated CN105 heat pump on the host, deterministically and with injectable faults and simulated time, so link and command behaviour can be tested without a real unit or board.
**Depends on**: Phase 1 (host test harness and characterization tests)
**Requirements**: SIM-01, SIM-02, SIM-03, SIM-04, SIM-05
**Success Criteria** (what must be TRUE):
  1. A GoogleTest scenario runs the real `CN105Climate` against the simulator through a fake UART: the 0x5A/0x7A handshake completes, a full poll cycle (info requests 0x02/0x03/0x04/0x06) is answered, and the state published by the component matches the simulator's internal state.
  2. A scenario applies a user command: the simulator receives the 0x41 packet, answers ACK 0x61, and its internal state is reflected consistently in the next 0x02 readback.
  3. A test can inject each fault (lost response, late response, corrupted checksum, disconnect, missing ACK) and observe the component's reaction deterministically, today's stalls and lost commands included; the fixes come in later phases.
  4. Scenarios can replay real captured frames (those already embedded in the unit tests and their debug-log headers) and run on simulated time: timeouts and the 32-bit `millis()` wraparound are crossed by advancing a test-controlled clock, with no real-time waits.
  5. Phase gate: all pre-existing unit tests pass, coverage of `components/cn105/` is not below the baseline and the new code is covered, and the 7 CI ESPHome configs still compile.
**Plans**: TBD

### Phase 3: ESPHome Host End-to-End Job
**Goal**: CI exercises the real ESPHome build and main loop (YAML schema, codegen and `loop()`) against the simulator, catching breakage that GoogleTest cannot see.
**Depends on**: Phase 2
**Requirements**: SIM-06
**Success Criteria** (what must be TRUE):
  1. A CI job compiles an example config for the ESPHome `host` platform through the component's real YAML schema and codegen, and the build succeeds.
  2. The compiled binary runs against the simulator over a virtual serial port (pty) and, as asserted by the job from the component's output, completes the handshake, runs at least one poll cycle and performs one command round trip.
  3. Breaking the YAML schema or the simulator link makes the job fail (demonstrated once), and a developer can reproduce the job locally with a documented command.
  4. Phase gate: the existing unit-test job and the 7 CI ESPHome compile configs are unaffected and still pass.
**Plans**: TBD

### Phase 4: Critical Timing Fixes
**Goal**: A heat pump polled for weeks no longer freezes at the `millis()` wraparound, and the remote-vs-internal temperature decision honours the configured margin. Small enough to ship as an early hotfix.
**Depends on**: Phase 2 (simulator and simulated time; Phase 1 characterization tests)
**Requirements**: TIME-01, TIME-02, TIME-03, TIME-04
**Success Criteria** (what must be TRUE):
  1. With the clock set just before and just after the 32-bit wrap, polling keeps cycling and the next cycle starts after `update_interval`, both in a unit test and end to end through the simulator (previously it stalled until the next user command).
  2. A deferred cycle resumes after its delay across the wrap, and a cycle already in progress at the wrap still expires on timeout.
  3. All timestamps and `millis()` deltas are `uint32_t`, results are identical whether the clock starts near 0 or above 2^31, and a freshly constructed `CN105Climate` has every field at a defined value (asserted by a unit test).
  4. With a 0.6 °C gap between remote and internal temperature and a 0.4 °C margin, the decision is "internal" (unit test), so the remote-temperature binary sensor honours `remote_temperature_margin`.
  5. Phase gate: each fixed bug has its own new test (Phase 1 assertions that pinned the old behaviour are flipped deliberately), all pre-existing tests pass, coverage is not below the baseline with changed lines covered, the 7 CI configs compile, and no YAML key or entity ID changed.
**Plans**: TBD

### Phase 5: Link Reliability
**Goal**: The heat pump link recovers cleanly from errors, a single missing response can no longer stall polling, and Home Assistant shows when the heat pump stops responding.
**Depends on**: Phase 4
**Requirements**: LINK-01, LINK-02, LINK-03, LINK-04, LINK-05, LINK-06
**Success Criteria** (what must be TRUE):
  1. Under simulated disconnects and repeated write failures only one reconnect runs at a time (debounced through the connection state machine), and a pending user command is never replaced by an info request; info requests are never queued.
  2. A single lost or late response does not stall the poll cycle: every poll request has a short bounded timeout, and a late response that does not match the current request is consumed and ignored without restarting or duplicating the request chain (simulator fault scenarios).
  3. Human-verified on the user's real board before the fix: the UART port actually used by the CN105 is logged and compared with the port `force_low_level_uart_reinit` targets, and the outcome is recorded. The reinit then either targets the real port (never falling back to UART0 or GPIO1/2) or is removed if it brings nothing, and the logger keeps working after a forced reinit.
  4. When the simulated heat pump stops responding, Home Assistant shows it (component warning status and/or a connectivity `binary_sensor` going off or unavailable), and it clears when responses resume.
  5. Phase gate: each fixed bug has a new test, all pre-existing tests pass, coverage is not below the baseline with changed lines covered, the 7 CI configs compile, no existing YAML key or entity ID changed, and the new connectivity entity or status is documented in the README.
**Plans**: TBD

### Phase 6: Command Reliability & Diagnostics
**Goal**: A user command is either applied or visibly reported as failed, Home Assistant never shows an optimistic state the heat pump did not accept, and link health is visible as diagnostics.
**Depends on**: Phase 5
**Requirements**: CMD-01, CMD-02, CMD-03, CMD-04, DIAG-01, DIAG-02
**Success Criteria** (what must be TRUE):
  1. A command that receives no ACK is resent up to N times, and a command is considered applied on ACK `0x61` or on a consistent `0x02` readback and then stops being resent; the tracking logic is a pure function with its own unit tests, and the behaviour is exercised end to end with the simulator's missing-ACK fault.
  2. After N failed attempts a warning is raised and Home Assistant shows the heat pump's real state again instead of the optimistic requested one.
  3. While a packet is only queued (connection inactive), the wanted state is neither cleared nor published as applied.
  4. Link diagnostic sensors (checksum errors, timed-out cycles, reconnects, average latency) and a defrost `binary_sensor` derived from the `DEFROST` sub-mode are available, and their values move under simulator-injected faults and sub-mode changes; they are additive and opt-in, so no existing entity ID changes.
  5. Phase gate: each fixed bug has a new test, all pre-existing tests pass, coverage is not below the baseline with changed lines covered, the 7 CI configs compile, and the new behaviour and sensors are documented in the README.
**Plans**: TBD

### Phase 7: Cleanup & CI Hardening
**Goal**: Dead weight is gone, the source is readable, and CI catches upcoming ESPHome breakage and memory or undefined-behaviour errors early.
**Depends on**: Phase 4 (independent of Phases 5 and 6)
**Requirements**: CLEAN-01, CLEAN-02, CLEAN-03, CLEAN-04, CI-01, CI-02
**Success Criteria** (what must be TRUE):
  1. `std::mutex` and `emulateMutex` are gone and the control and command paths behave identically on ESP32 and ESP8266 (both still compile; characterization tests and simulator scenarios pass unchanged).
  2. A reviewer can read the source: the listed dead code (commented blocks, `buildAndSendRequestPacket`, empty `RequestScheduler::loop()`, `TEST_MODE`, doubtful `hpPacketDebug` comments) no longer exists, comments are valid UTF-8 with no mojibake, and log messages are in English.
  3. `tests/unit/test_checksum.cpp` exercises the production `checkSum()`: changing the production checksum makes the test fail.
  4. CI also compiles against ESPHome `beta` as continue-on-error (a failure is visible but does not block merge), and the unit tests build with `-Wall -Wextra -Werror` and pass under ASan and UBSan.
  5. Phase gate: all pre-existing tests pass, coverage is not below the baseline, the 7 CI configs compile, no existing YAML key or entity ID changed, and behaviour on ESP32 and ESP8266 is unchanged.
**Plans**: TBD

### Phase 8: Strong Typing & Encapsulation
**Goal**: Component state is represented by types the compiler checks, and `CN105Climate`'s internals are no longer reachable from outside the class.
**Depends on**: Phase 6, Phase 7
**Requirements**: TYPE-01, TYPE-02
**Success Criteria** (what must be TRUE):
  1. Mode, power, fan and vane states are `enum class` values converted to strings only at the HA and log boundary; no `strcmp` on state remains in the component, and Home Assistant shows exactly the same values as before.
  2. `CN105Climate` has no public data fields, and the protocol profiles reach its internals through an interface rather than `friend` access.
  3. The refactor is behaviour-preserving: characterization tests, simulator scenarios and the host end-to-end job pass with no semantic changes to their assertions (type-only edits at most).
  4. Phase gate: coverage is not below the baseline with changed lines covered, the 7 CI configs compile, no existing YAML key or entity ID changed, and any user-visible change is documented in the README.
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Coverage Safety Net | 0/13 | Planned | - |
| 2. Heat Pump Simulator | 0/TBD | Not started | - |
| 3. ESPHome Host End-to-End Job | 0/TBD | Not started | - |
| 4. Critical Timing Fixes | 0/TBD | Not started | - |
| 5. Link Reliability | 0/TBD | Not started | - |
| 6. Command Reliability & Diagnostics | 0/TBD | Not started | - |
| 7. Cleanup & CI Hardening | 0/TBD | Not started | - |
| 8. Strong Typing & Encapsulation | 0/TBD | Not started | - |

## Planning Notes

Constraints and risks surfaced while roadmapping, for `/gsd-plan-phase`:

- **Where the injectable clock lands.** `CUSTOM_MILLIS` expands to `esphome::millis()` (`components/cn105/Globals.h`), so a test-build stub of `esphome::millis()` gives a time seam without touching production code. Phase 1 builds that test-only seam (characterization tests need to drive time and must pin unmodified code), Phase 2 builds the simulated clock on it (SIM-05, including wrap), and Phase 4 delivers the production wrap-safe logic and the clock abstraction (TIME-01 / DEBT-07). Each requirement still maps to exactly one phase.
- **Phase 1 harness is the hardest part of the safety net.** `cycle_management.cpp`, `request_scheduler.cpp`, `hp_writings.cpp`, `climateControls.cpp` and `heatpump_profile.cpp` are not in the unit-test build today (they include `cn105.h` and `<esphome.h>`). COV-04 therefore requires getting `CN105Climate` to compile under GoogleTest (hand-written ESPHome stubs versus real ESPHome host headers is a plan-time decision). Phase 2 (SIM-02) builds on this harness; Phase 3 reuses the same decision for the `host` build.
- **SIM-04 source data.** `captures/` currently holds only screenshots and photos, not frame dumps. Replay material is the real frames already embedded in `tests/unit/test_real_frames.cpp` and the debug logs quoted in test headers; new captures can be added if the user records them.
- **LINK-05 needs the board first.** The BUG-05 verification on the user's ESP32 (ESP-IDF, logger on UART0, one CN105 `uart:`) is the first step of Phase 5 and decides between fixing the port lookup and removing the low-level reinit.
- **Phase 8 may be a large PR.** TYPE-01 (enum class across all profiles) and TYPE-02 (encapsulating ~60 fields, removing `friend`) are kept together per the audit's P5; if the diff is too large to review at plan time, split it into two plans merged as two PRs.
- **Phase 7 ordering.** It depends only on Phase 4, so it can be pulled ahead of Phases 5 and 6 if merge conflicts in `hp_writings.cpp` / `request_scheduler.cpp` (mutex and dead-code removal) outweigh the benefit of the audit's order.
