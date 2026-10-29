---
gsd_state_version: "1.0"
current_phase: 1
current_phase_name: Coverage Safety Net
status: executing
stopped_at: Completed 01-03-PLAN.md
last_updated: "2026-10-29T20:42:26.598Z"
last_activity: 2026-10-29
last_activity_desc: Phase 1 execution started
state_head: f0cc694008dcf27bc6b99a1736176d667780dbc4
progress:
  total_phases: 8
  completed_phases: 0
  total_plans: 15
  completed_plans: 4
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-10-27)

**Core value:** The heat pump link never silently stalls or lies: polling keeps running indefinitely, user commands are either applied or visibly reported as failed, and Home Assistant shows the heat pump's real state (or "unavailable"), never stale or optimistic values.
**Current focus:** Phase 1 — Coverage Safety Net

## Current Position

Phase: 1 (Coverage Safety Net) — EXECUTING
Plan: 5 of 15
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

Last session: 2026-10-29T20:42:26.568Z
Stopped at: Completed 01-03-PLAN.md
Resume file: None
