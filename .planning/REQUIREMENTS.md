# Requirements: MitsubishiCN105ESPHome — Reliability & Architecture Milestone

**Defined:** 2026-10-27
**Core Value:** The heat pump link never silently stalls or lies: polling keeps running indefinitely, user commands are either applied or visibly reported as failed, and Home Assistant shows the heat pump's real state (or "unavailable"), never stale or optimistic values.

Source: `AUDIT-CN105-2026-10-27.md` (bug/debt/feature IDs in parentheses).

## v1 Requirements

### Coverage safety net (before any fix or refactor)

- [ ] **COV-01**: CI measures line and branch coverage of `components/cn105/` from the unit tests (gcov/lcov or gcovr) and publishes the report (job summary + artifact)
- [ ] **COV-02**: A coverage baseline is recorded and CI fails if total coverage of `components/cn105/` drops below it (ratchet: the baseline only goes up)
- [ ] **COV-03**: Each PR reports coverage of changed lines (diff coverage), so untested modifications are visible in review
- [ ] **COV-04**: Characterization tests pin the current behaviour of the code the milestone will change (`cycle_management`, `request_scheduler`, `hp_writings`/`writePacket`, `climateControls`, state handling in the profiles) before it is modified

### Timing (audit P1)

- [ ] **TIME-01**: Polling keeps cycling across a `millis()` wraparound; a unit test with an injectable clock just before/after wrap shows the next cycle starts after `update_interval` (BUG-01, DEBT-07)
- [ ] **TIME-02**: A deferred cycle (`deferCycle`) and the timeout of an in-progress cycle both behave correctly across wraparound, covered by unit tests (BUG-01)
- [ ] **TIME-03**: All timestamps and `millis()` deltas use `uint32_t`, and every `CN105Climate` field is initialized at declaration (BUG-08)
- [ ] **TIME-04**: The remote-vs-internal temperature decision honours `remote_temperature_margin` — with a 0.6 °C gap and 0.4 °C margin the result is "internal", covered by a unit test (BUG-02)

### Link (audit P2)

- [ ] **LINK-01**: Only one reconnect runs at a time, debounced through the connection state machine (BUG-04)
- [ ] **LINK-02**: A pending user command is never replaced by an info request; info requests are never queued (BUG-04)
- [ ] **LINK-03**: Every poll request has a short timeout, so a single missing response cannot stall the cycle (BUG-07)
- [ ] **LINK-04**: A late response not matching the current request is consumed and ignored, never restarting the request chain (BUG-07)
- [ ] **LINK-05**: `force_low_level_uart_reinit` targets the UART port the CN105 actually uses and never falls back to UART0 or GPIO1/2 — verified on the board first, or the low-level reinit is removed if it brings nothing (BUG-05)
- [ ] **LINK-06**: When the heat pump stops responding, HA shows it (component warning status and/or connectivity `binary_sensor`) (FEAT-01)

### Commands (audit P3)

- [ ] **CMD-01**: A command that is not acknowledged is resent, up to N times (BUG-03, FEAT-04)
- [ ] **CMD-02**: A command is considered applied on ACK `0x61` or on a consistent `0x02` readback; the tracking logic is a pure, unit-tested function (BUG-03, FEAT-04)
- [ ] **CMD-03**: After N failed attempts, a warning is raised and HA shows the heat pump's real state again instead of the optimistic one (BUG-03, FEAT-04)
- [ ] **CMD-04**: The wanted state is not cleared or published as applied while the packet is only queued (BUG-03)

### Diagnostics (audit P3)

- [ ] **DIAG-01**: Link diagnostic sensors are available: checksum errors, timed-out cycles, reconnects, average latency (FEAT-05)
- [ ] **DIAG-02**: A defrost `binary_sensor` derived from the `DEFROST` sub-mode is available (FEAT-05)

### Cleanup (audit P4)

- [ ] **CLEAN-01**: `std::mutex` and `emulateMutex` are removed without changing behaviour on ESP32 or ESP8266 (DEBT-03)
- [ ] **CLEAN-02**: Dead code is removed — commented blocks, `buildAndSendRequestPacket`, empty `RequestScheduler::loop()`, `TEST_MODE`, doubtful comments in `hpPacketDebug` (DEBT-04)
- [ ] **CLEAN-03**: Source comments are valid UTF-8 and all log messages are in English (DEBT-05)
- [ ] **CLEAN-04**: `tests/unit/test_checksum.cpp` tests the production `checkSum()` instead of a reimplementation

### CI (audit P4)

- [ ] **CI-01**: CI also compiles against ESPHome `beta`, as continue-on-error (DEBT-06)
- [ ] **CI-02**: Unit tests build with `-Wall -Wextra -Werror` and run under ASan/UBSan (DEBT-06)

### Typing & Encapsulation (audit P5)

- [ ] **TYPE-01**: Mode, power, fan and vane states are `enum class` values, converted to strings only at the HA/log boundary; no `strcmp` on state (DEBT-02)
- [ ] **TYPE-02**: `CN105Climate` internals are encapsulated — no public data fields, and profiles use an interface instead of `friend` access to `hp_` internals (DEBT-01)

### Cross-cutting acceptance rules (every phase)

Not separate requirements; every phase's success criteria include them:
- All existing unit tests pass, and each fixed bug has a new unit test
- Coverage of `components/cn105/` does not drop below the baseline, and changed lines are covered by tests (diff coverage reviewed in the PR)
- All 7 CI ESPHome configs (ESP32 + ESP8266) compile
- No change to existing YAML keys or HA entity IDs (any rename uses a deprecation warning)
- User-visible additions are documented in the README
- One PR per phase, releasable on its own

## v2 Requirements

Deferred to the next milestone (audit P6).

### Features

- **FEAT-02**: Native climate presets (`SLEEP` for night mode, `BOOST`, `ECO`…)
- **FEAT-03**: Real HEAT_COOL range — setpoint recomputed on room temperature changes with hysteresis and rate limit (also fixes BUG-06)
- **BUG-09**: kWh counter wraparound handled (accumulate or `total_increasing`)
- **FEAT-07**: Multi-split mode conflict detection (issue #550)
- **FEAT-06**: Timers (0x05) — requires reverse-engineering the format first
- **FEAT-08**: DRY target humidity — verify protocol write support first

## Out of Scope

| Feature | Reason |
|---------|--------|
| Splitting `CN105Climate` into separate collaborator classes | Larger/riskier than finishing encapsulation; reconsider after TYPE-02 |
| Breaking changes to YAML keys or HA entity IDs | Existing users must upgrade transparently |
| P6 features in this milestone | Milestone is reliability + refactor; tracked as v2 |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| (filled by roadmap) | | |

**Coverage:**
- v1 requirements: 28 total
- Mapped to phases: 0
- Unmapped: 28 ⚠️

---
*Requirements defined: 2026-10-27*
*Last updated: 2026-10-27 after initial definition*
