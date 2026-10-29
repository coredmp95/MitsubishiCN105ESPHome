---
gsd_state_version: "1.0"
current_phase: 1
current_phase_name: Coverage Safety Net
status: executing
stopped_at: Completed 01-06-PLAN.md
last_updated: "2026-10-29T21:36:13.181Z"
last_activity: 2026-10-29
last_activity_desc: Phase 1 execution started
state_head: 9fb6cf4fae9d02ebd3a5c5f988faa32a32694fcf
progress:
  total_phases: 8
  completed_phases: 0
  total_plans: 15
  completed_plans: 10
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-10-27)

**Core value:** The heat pump link never silently stalls or lies: polling keeps running indefinitely, user commands are either applied or visibly reported as failed, and Home Assistant shows the heat pump's real state (or "unavailable"), never stale or optimistic values.
**Current focus:** Phase 1 — Coverage Safety Net

## Current Position

Phase: 1 (Coverage Safety Net) — EXECUTING
Plan: 11 of 15
Status: Ready to execute
Last activity: 2026-10-29 — Phase 1 execution started

Progress: [░░░░░░░░░░] 0%

## Performance Metrics

**Velocity:**
- Total plans completed: 0
- Average duration: - min
- Total execution time: 0.0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| - | - | - | - |

**Recent Trend:**
- Last 5 plans: -
- Trend: -

*Updated after each plan completion*
**Per-Plan Metrics:**

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 01 P01 | 35 min | 3 tasks | 37 files |
| Phase 01 P04 | 30 min | 3 tasks | 2 files |
| Phase 01 P02 | 25min | 2 tasks | 8 files |
| Phase 1 P03 | 25 min | 2 tasks | 12 files |
| Phase 01 P07 | 35min | 3 tasks | 2 files |
| Phase 01 P08 | 40 min | 3 tasks | 1 files |
| Phase 01 P09 | 35min | 3 tasks | 1 files |
| Phase 01 P10 | 45 min | 3 tasks | 1 files |
| Phase 01 P05 | 7 min | 3 tasks | 6 files |
| Phase 01 P06 | 12 min | 3 tasks | 4 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Roadmap]: Safety net first - coverage + characterization (Phase 1), simulator (Phases 2-3) before any fix or refactor
- [Roadmap]: Injectable clock split across phases - test-only `esphome::millis()` seam (Phase 1), simulated clock (Phase 2), production wrap-safe logic + clock abstraction (Phase 4)
- [Roadmap]: Audit P1-P5 order kept after the safety net; Phase 7 (cleanup/CI) depends only on Phase 4
- [Roadmap]: One PR per phase, each releasable on its own; no YAML/entity-ID breaks
- [Phase 01]: 01-01: pure-test include dirs moved to target scope; CN105_HARNESS ON only for 32-bit builds
- [Phase 01]: 01-01: harness preference backends owned by a static pool so ESPPreferenceObject pointers survive TestPrefs::clear()
- [Phase 01]: 01-04: coverage_gate.py uses exact Fraction arithmetic on counts; a 0.1-point drop passes, FAIL > RATCHET > OK; lowered baseline fails unless toolchain changed; complete only adds manifest-listed headers as 0/0
- [Phase 01]: 01-02: coverage header TUs generated with file(GENERATE) (keeps cmake 3.14 minimum); run-harness.sh runs the char binary directly with gtest names
- [Phase 1]: 01-03: ESP8266 harness variant compiles the volatile-bool emulateMutex branch, pinned by a static_assert on wantedSettingsMutex per variant
- [Phase 1]: 01-03: harness force-includes use SHELL:-include pairs because CMake de-duplicates repeated bare -include options
- [Phase 01]: 01-07: late 0x03 response while 0x06 awaited pinned as a duplicated 0x06 request (LINK-04); BUG-01 and BUG-07 pinned on both variants
- [Phase 01]: 01-08: 'UART not ready' retry is only reachable with an unconfigurable UART; no production path returns the FSM below CONNECTING after boot
- [Phase 01]: 01-08: try_write_pending_packet clears has_pending_packet_ after a re-queue, so a SET packet is lost if the link is still inactive at the 4000 ms retry (pinned, LINK-02)
- [Phase 01]: 01-09: BUG-08 debounce arithmetic keeps working past 2^31 under -m32 through signed wrap-around (UB); Phase 4 flip is behaviour-neutral
- [Phase 01]: 01-09: emulateMutex retry schedule pinned (deferrals at 0,100,220,364,536,742,989,1285,1640,2066 ms, forced at 2577 ms); Phase 7 CLEAN-01 removes it
- [Phase 01]: 01-10: dead guard logs (pending user temp, post-send grace) pinned as absent; Lossnay connected state via real rejected reply + handleConnectionSuccess(), no invented frame
- [Phase 01]: 01-05: coverage_gate.py complete also lists a manifest-listed .cpp with no executable line (info_request.cpp) as 0/0; coverage.sh manifest lists header TUs and .cpp objects present in both variants
- [Phase 01]: 01-05: coverage.sh exit precedence test failure (1) > scope (3) > ratchet gate (5); --ci skips virtualenv and Docker re-exec
- [Phase 01]: 01-06: upload-artifact@v7 and download-artifact@v8 (independent latest majors); sticky PR comment built for every PR but posted only for same-repo PRs, comment failure is a warning

### Pending Todos

None yet.

### Blockers/Concerns

- [Phase 1]: `CN105Climate` is not in the unit-test build today; getting it to compile under GoogleTest (stubs vs real ESPHome host headers) is the main risk of the safety net and is reused by Phases 2-3
- [Phase 2]: `captures/` holds only images, not frame dumps; replay uses frames already embedded in `tests/unit/`
- [Phase 5]: LINK-05 (BUG-05) must be verified on the user's real ESP32 board before any fix
- [Phase 8]: TYPE-01 + TYPE-02 together may be a very large PR; be ready to split at plan time

## Deferred Items

Items acknowledged and deferred at milestone close, most recent first:

| Category | Item | Status | Deferred At | Milestone |
|----------|------|--------|-------------|-----------|
| Features | Audit P6: FEAT-02/03/06/07/08, BUG-09 (BUG-06 via FEAT-03) | Deferred to next milestone | 2026-10-27 | Reliability & Architecture |

## Session Continuity

Last session: 2026-10-29T21:36:13.150Z
Stopped at: Completed 01-06-PLAN.md
Resume file: None
