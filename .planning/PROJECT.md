# MitsubishiCN105ESPHome — Reliability & Architecture Milestone

## What This Is

An ESPHome external component (`components/cn105/`) that controls Mitsubishi heat pumps and air conditioners over the CN105 connector (UART 8E1, 2400 baud), exposing a Home Assistant `climate` entity plus vanes, power/energy sensors, sub-modes, error codes, installer functions, Fahrenheit and Lossnay support. A companion HA integration (`custom_components/mitsubishi_climate_proxy/`) handles single/dual setpoint display. This milestone fixes the bugs found in the 2026-10-27 audit, then refactors the component's internals, without changing anything users configure.

## Core Value

The heat pump link never silently stalls or lies: polling keeps running indefinitely, user commands are either applied or visibly reported as failed, and Home Assistant shows the heat pump's real state (or "unavailable"), never stale or optimistic values.

## Requirements

### Validated

<!-- Inferred from existing code (codebase map, commit 5250ba7). -->

- ✓ CN105 protocol driver with connection state machine (`DriverState`), separate `RequestScheduler`, and protocol profiles behind a common interface (heat pump, Lossnay) — existing
- ✓ Pure, ESPHome-independent protocol logic in `*_protocol.h` and `frame_parser.h`, covered by ~287 GoogleTest cases including real captured frames and issue regressions — existing
- ✓ HA `climate` entity with dual setpoint, vanes (vertical/horizontal), i-See, stage and sub-mode, power and energy sensors, error codes, installer functions, Fahrenheit, Lossnay — existing
- ✓ Tolerant frame decoding: unknown byte keeps previous value, logged once — existing
- ✓ Remote temperature with watchdog fallback to the internal sensor, keep-alive, debounce — existing
- ✓ UI anti-bounce grace windows after commands so the unit doesn't overwrite the user's setpoint — existing
- ✓ ESPHome version compatibility layer (`esphome_compat.h`) for 2025.7 → 2026.x — existing
- ✓ CI: unit tests then compile of 7 ESPHome configs (ESP32 + ESP8266) on `esphome:stable` — existing

### Active

<!-- This milestone = audit phases P1–P5. IDs refer to AUDIT-CN105-2026-10-27.md. -->

- [ ] Coverage safety net first: coverage measured in CI with a ratcheting baseline, diff coverage on PRs, and characterization tests pinning current behaviour of the code to be changed
- [ ] Critical fixes: `millis()` wrap-safe timing with an injectable clock (BUG-01 + DEBT-07), `std::fabs` for remote-temp margin (BUG-02), initialized fields and `uint32_t` timestamps (BUG-08)
- [ ] Link reliability: single debounced reconnect and user commands never displaced by info requests (BUG-04), short timeouts and ignoring out-of-sequence responses in the poll cycle (BUG-07), verify-on-board then fix UART port targeting of `force_low_level_uart_reinit` (BUG-05), heat pump unavailability visible in HA (FEAT-01)
- [ ] Command reliability: in-flight command tracking, confirmation on ACK 0x61 or consistent 0x02 readback, N retries then warning and republish of real state (BUG-03 / FEAT-04); link diagnostics sensors and defrost binary sensor (FEAT-05)
- [ ] Cleanup: remove useless mutex / `emulateMutex` (DEBT-03), dead code (DEBT-04), UTF-8 comments and English-only logs (DEBT-05), CI with ESPHome `beta` (continue-on-error), `-Wall -Wextra -Werror` and ASan/UBSan on unit tests (DEBT-06)
- [ ] Strong typing and encapsulation: `enum class` for mode/power/fan/vane states instead of `const char*` + `strcmp` (DEBT-02), then finish moving `CN105Climate` internals behind the protocol profiles (DEBT-01)

### Out of Scope

- Audit P6 features — native climate presets (FEAT-02), real HEAT_COOL range (FEAT-03, which also fixes BUG-06), 16-bit kWh wrap (BUG-09), multi-split mode conflict (FEAT-07), timers 0x05 (FEAT-06), DRY target humidity (FEAT-08) — deferred to the next milestone; this one is reliability + refactor
- Splitting `CN105Climate` into separate collaborator classes (link, scheduler, state model, entities) — larger and riskier than P5; reconsider after encapsulation is finished
- Breaking changes to YAML keys or HA entity IDs — users must be able to update without editing configs

## Context

- Brownfield; codebase map in `.planning/codebase/` (STACK, ARCHITECTURE, STRUCTURE, CONVENTIONS, TESTING, INTEGRATIONS, CONCERNS), mapped at commit `5250ba7`.
- Source of the work: `AUDIT-CN105-2026-10-27.md` (repo root, French). It gives cause, expected fix and acceptance criteria per bug, and proposes the phase split P1–P6 this milestone follows (P1 critical → P2 link → P3 commands; P4 cleanup after P1, independent of P2/P3; P5 typing after P3 and P4).
- Bugs confirmed in code by the concerns mapper: BUG-01, BUG-02, BUG-03, BUG-04, BUG-09. BUG-05 needs on-board verification; BUG-06/07 are from code reading.
- Architecture anti-patterns noted in the map: god class `CN105Climate` across many translation units (~60 public fields, `friend class`), duplicate/stale protocol-state flags, raw protocol bytes written outside the write path, mojibake comments.
- `tests/unit/test_checksum.cpp` reimplements `checkSum()` instead of testing production code. No formatter/linter config is tracked.
- Repo is the user's fork (`coredmp95/MitsubishiCN105ESPHome`) of the upstream component; history is PR-merge based.
- The user has a real heat pump unit and an ESP board for on-device verification.

## Constraints

- **Compatibility**: No breaking changes to YAML keys or HA entities; any rename goes through a deprecation warning — existing users must upgrade transparently.
- **Tests**: All existing unit tests keep passing, and every fixed bug gets a new unit test — the test suite is the main safety net for protocol logic.
- **Coverage**: Coverage of `components/cn105/` never drops below the recorded baseline, and changed lines are tested — the user's assurance that nothing breaks.
- **CI**: All 7 CI ESPHome configs (ESP32 + ESP8266) must compile — ESP8266 RAM/flash limits and multi-framework support (ESP-IDF, Arduino) are real.
- **Testability**: New logic goes into pure functions (`*_protocol.h` style) testable without ESPHome — keeps it under GoogleTest.
- **Platform**: ESPHome ≥ 2025.7 must keep working via `esphome_compat.h` — supported user base.
- **Docs**: User-visible changes are documented in the README — per the audit's definition of done.
- **Delivery**: One PR per phase, each releasable on its own — keeps reviews small and lets fixes ship early.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Follow audit phases P1–P5 for this milestone | Audit already orders fixes by severity and dependency | — Pending |
| Fix bugs through their paired features (FEAT-01, FEAT-04, FEAT-05) | The proper fix for BUG-03/04 *is* those features | — Pending |
| Defer P6 features (incl. BUG-06, BUG-09) to next milestone | Keep this milestone to reliability + refactor | — Pending |
| Refactor = cleanup + enum class + finish profile encapsulation, not a god-class split | Lower risk; split can be reconsidered after | — Pending |
| Injectable clock replaces `CUSTOM_MILLIS` macro | Needed to unit-test wraparound (BUG-01) | — Pending |
| Verify BUG-05 on the real board before fixing | Audit marks it unconfirmed; user has hardware | — Pending |
| One PR per phase, no breaking changes | Reviewable, releasable increments for existing users | — Pending |
| Coverage safety net before fixes/refactor (ratchet baseline + diff coverage + characterization tests) | User wants confidence nothing breaks; the refactor touches poorly isolated code | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-10-27 after initialization*
