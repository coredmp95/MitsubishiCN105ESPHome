---
phase: 01-coverage-safety-net
plan: 13
subsystem: testing
tags: [googletest, characterization, connection-lifecycle, read-path, bug-05, bug-08, m32, esp32, esp8266]

requires:
  - phase: 01-coverage-safety-net
    provides: "01-01..01-03 fixture API (Cn105Fixture, TestWifi, IdfRecorder, TestClock, TestLog), real_frames.h, ESP32 + ESP8266 -m32 harness variants; 01-12 capture decision existing-only and the Wanted captures list"
provides:
  - "ConnectionLifecycle suites: 8 tests on ESP8266, 11 on ESP32 (ESP32 adds the three recorded-IDF-call tests)"
  - "WAIT_WIFI -> WAIT_GRACE -> CONNECTING -> CONNECTED pinned through the real state machine, 120 s bootstrap timeout, first-connection retry, installer 0x5B to 0x5A fallback, reconnectIfConnectionLost gates, 10 x update_interval window"
  - "Characterize_BUG08_ConnectionActiveSignedOverflow (flip in Phase 4, TIME-03) and Characterize_BUG05_LowLevelReinitTargetsUart0Pins1And2 (flip in Phase 5, LINK-05, after the on-board check)"
  - "ReadingsDecode suites: 10 tests per variant (checksum drop, unknown command and info code, oversized frame, 0x04 error info, 0x09 stage / sub-mode / auto sub-mode, room temperature encodings, duplicate status)"
affects: [01-14, phase-04-time, phase-05-link]

actuals:
  tokens: 10000
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "ConnectionLifecycle base fixture with frames_until(ms, answer_polls): steps 1 ms at a time and returns (clock, frame) pairs, so exact write times are pinned; answer_polls keeps the link alive with the default real responses"
    - "ESP32-only timing: force_low_level_uart_reinit advances the test clock by 4 ms (two CUSTOM_DELAY(2)); pinned through kReinitDelayMs, 0 on ESP8266, instead of two copies of every expectation"
    - "Negative tests feed a real frame modified on purpose and say which byte changed; none is presented as a capture"

key-files:
  created:
    - tests/unit/characterization/test_connection_lifecycle.cpp
    - tests/unit/characterization/test_readings_decode.cpp
  modified:
    - tests/unit/README.md

key-decisions:
  - "BUG-08 is pinned as observed, not as the plan described it: a gap above 2^31 ms leaves isHeatpumpConnectionActive() false (inactive), because the signed long delta is compared with a uint32_t and converted back to unsigned. The visible symptom of the signed delta is a negative duration in the reconnect warning (-2147483 s). The test keeps the name required by the plan"
  - "No DEFROST test and no new captured frame: plan 01-12 recorded existing-only, so DEFROST stays under Wanted captures. A new wanted scenario (a 0x04 reply with a non-zero error code) was added as row 12"
  - "Pins whose timing depends on the variant use spacing or kReinitDelayMs, never an absolute time that differs between ESP32 and ESP8266"

patterns-established:
  - "A pin that contradicts the plan's expectation is written as observed, with a comment stating the difference, and reported in the SUMMARY"

requirements-completed: [COV-04]

coverage:
  - id: D1
    description: "Connection state machine pinned end to end: BOOT -> WAIT_WIFI while Wi-Fi is down, WAIT_GRACE, CONNECT frame, CONNECTED; 120 s bootstrap timeout forces CONNECT without Wi-Fi and is ignored once connecting"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ConnectionLifecycleWifi.Characterize_WaitsForWifiBeforeConnect, Characterize_BootstrapTimeoutForcesConnectWithoutWifi; ConnectionLifecycle.Characterize_BootstrapTimeoutIgnoredOnceConnecting (tests/unit/characterization/test_connection_lifecycle.cpp)"
        status: pass
    human_judgment: false
  - id: D2
    description: "First-connection retry every 10 s, installer handshake falls back to the standard handshake once, reconnectIfConnectionLost gates (update_interval, strict boundaries) and the 10 x update_interval activity window"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ConnectionLifecycle.Characterize_FirstConnectionRetryWithoutReply, Characterize_ReconnectIfConnectionLostWaitsUpdateInterval, Characterize_ConnectionInactiveAfterTenIntervals; ConnectionLifecycleInstaller.Characterize_InstallerHandshakeFallsBackToStandardOnce"
        status: pass
    human_judgment: false
  - id: D3
    description: "BUG-08 (signed delta, flip in Phase 4 TIME-03) and BUG-05 (UART0 with GPIO1/GPIO2 when port and pins are unset, ESP32 stub record, flip in Phase 5 LINK-05 after the on-board check) pinned, plus the configured port/pins and unknown-port fallback"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ConnectionLifecycle.Characterize_BUG08_ConnectionActiveSignedOverflow, Characterize_BUG05_LowLevelReinitTargetsUart0Pins1And2, Characterize_LowLevelReinitUnknownPortFallsBackToUart0; ConnectionLifecycleConfiguredUart.Characterize_LowLevelReinitUsesConfiguredPortAndPins"
        status: pass
    human_judgment: false
  - id: D4
    description: "Read path pinned: checksum mismatch dropped with its warning, unknown command ignored, unknown info code logged, oversized frame reset, 0x04 error info, 0x09 stage / sub-mode / auto sub-mode from real frames, both room temperature encodings, duplicate status not republished"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "ReadingsDecode.* and ReadingsDecodeSensors.* (tests/unit/characterization/test_readings_decode.cpp)"
        status: pass
    human_judgment: false
  - id: D5
    description: "No invented capture, DEFROST not pinned, components/cn105 unchanged"
    requirement: COV-04
    verification:
      - kind: other
        ref: "git diff --quiet origin/main -- components/cn105; grep -c Characterize_Defrost test_readings_decode.cpp = 0"
        status: pass
    human_judgment: false

duration: ~75min
completed: 2026-10-29
status: complete
plan_head_before: 66139d9279e4cbb5d3e35b7d6b61f1929cd225f4
plan_head_after: f938f83a679e730c3ef040be3102b3fe13ff76e8
commits: 3
---

# Phase 1 Plan 13: Connection lifecycle and read-path characterization Summary

**The connection state machine (Wi-Fi gate, 120 s bootstrap timeout, retry, reconnect gates, activity window), the low-level UART reinit that BUG-05 suspects and the whole read/decode path of hp_readings.cpp are pinned on both variants, with BUG-08 pinned as it really behaves and no invented frame.**

## Performance

- **Duration:** ~75 min (start time not captured; estimated)
- **Completed:** 2026-10-29
- **Tasks:** 3 (1 tracer, 2 auto)
- **Files:** 2 created (`test_connection_lifecycle.cpp`, `test_readings_decode.cpp`), 1 modified (`tests/unit/README.md`, one Wanted captures row)

## Accomplishments

- `ConnectionLifecycle*`: 8 tests on ESP8266, 11 on ESP32. `ReadingsDecode*`: 10 tests per variant.
- Full harness run: ESP32 132, ESP8266 130, all pass, `[run-harness] OK: esp32 esp8266`.
- Tracer gate (automated-only verify, end-of-phase mode): Task 1 verify passed on both variants before expanding.
- `components/cn105` unchanged (`git diff --quiet origin/main -- components/cn105` clean, D-03).

## Task Commits

1. **Task 1 (tracer): Wi-Fi gated connection end to end** - `b6c2dac` (test)
2. **Task 2: bootstrap timeout, retry, reconnect timing, BUG-08 and BUG-05** - `8771452` (test)
3. **Task 3: read and decode paths** - `f938f83` (test)

## Observations pinned (asserted as observed)

- **Grace delay counts from boot.** `boot_ms_` is set in `setup()`, so after Wi-Fi comes back at t = 60 s the state goes WAIT_WIFI -> WAIT_GRACE on the next loop and CONNECT leaves on the loop after (not 10 s later as the plan expected).
- **Bootstrap timeout.** Armed by the first loop; CONNECT is written at t = 120001 ms with a warning log and the FSM goes WAIT_WIFI -> CONNECTING directly (WAIT_GRACE is skipped). Once the link is up the timeout does nothing.
- **First-connection retry.** One CONNECT every 10 s (10 004 ms on ESP32: `force_low_level_uart_reinit` calls `CUSTOM_DELAY(2)` twice and the test clock really advances by 4 ms; 10 000 ms on ESP8266). The FSM stays in CONNECTING, no DISCONNECTED transition. Installer mode: first CONNECT is `FC 5B`, then exactly one fallback to `FC 5A`.
- **reconnectIfConnectionLost.** With 9999 ms of silence nothing happens; at 10 000 ms CONNECT leaves and the FSM goes CONNECTED -> DISCONNECTED -> CONNECTING. The next attempt needs `now - lastReconnectTimeMs >= update_interval` AND `now - lastConnectRqTimeMs > update_interval` (strict): at exactly +1000 ms nothing, at +1001 ms CONNECT.
- **Natural loss path.** A silent heat pump is detected by `writePacket()` (not by `loop()`): about 14 s after the last reply the next info request fails the activity check and `reconnectUART()` runs. State stays CONNECTED between the last reply and that write.
- **BUG-05 (ESP32 stub record).** With no `set_uart_port` and no `set_tx_rx_pins`, `reconnectUART()` records `uart_param_config(0, 2400)` then `uart_set_pin(0, tx 1, rx 2, rts -1)`, no `gpio_reset_pin`, no pull-up. A port other than 1 or 2 falls back to port 0. With port 2 and pins 17/16: `gpio_reset_pin(17)`, `gpio_reset_pin(16)`, `uart_param_config(2, 2400)`, `uart_set_pin(2, 17, 16, -1)`, `gpio_set_pull_mode(16, ...)`. These are stub records, not driver behaviour (T-01-26): Phase 5 must check on a real board first.
- **Checksum mismatch.** Warning `KO -> checksum mismatch (cmd=0x62 len=16)` under the `chkSum` tag; the frame is dropped and `lastResponseMs` does not move. A valid checksum on an unknown command (0x63) still moves `lastResponseMs` and decodes nothing.
- **Unknown info codes.** `0x77` logs a warning `packet type [77] <-- unknown and unexpected`; `0x10` logs a debug line. `0x05` never reaches the switch: the "timers" request is registered, the scheduler consumes the reply and nothing is written outside a cycle (the `[0x05 is Timer : not implemented]` branch is dead).
- **Oversized frame.** A header declaring 0xFF payload bytes is reset at the fifth byte by the sanity check; the `bytes_read_ >= MAX_DATA_BYTES` guard is unreachable through `feed()`. The next real frame decodes.
- **0x04 and 0x09.** `ERROR_INFO_0x80` decodes to `No Error` (bit 7 only announces error reporting). POWER_IDLE / POWER_LOW / POWER_OFF_SUBMODE_0x10 give stage IDLE / LOW / IDLE, sub-mode NORMAL / NORMAL / OFF, auto sub-mode AUTO_OFF; a stage change alone does not publish the climate, a sub-mode change does.
- **Room temperature.** Real ROOM_TEMP_23C decodes to 23.0 through data[6]; with data[6] = 0 the legacy byte data[3] = 0x0D gives 23.0; an unknown legacy byte logs `Unknown room_temp byte 0xFF` and keeps the previous value. Outside temperature is NaN (byte 0).
- **Duplicate status.** Two further identical cycles publish nothing (`heatpumpStatus::operator==` treats NaN as equal to NaN).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Plan expectation] BUG-08 does not make the link look active**
- **Found during:** Task 2 first run
- **Issue:** The plan (truth and task action) expects `isHeatpumpConnectionActive()` to return true for a response gap above 2^31 ms. On the real component it returns false: `lrTimeMs` is a negative `long` but is compared with `MAX_DELAY_RESPONSE_FACTOR * update_interval_` (a `uint32_t`), so the usual arithmetic conversions turn the comparison unsigned. The same holds in `reconnectIfConnectionLost()`.
- **Fix:** `Characterize_BUG08_ConnectionActiveSignedOverflow` (name kept as required) pins what happens: gaps of 2^31 - 1, 2^31 and 2^32 - 1 ms are all inactive, and the reconnect warning prints the gap through the signed `long` as `-2147483 s`. A comment states the difference from the plan. The flip comment `Pins current buggy behaviour — flip in Phase 4 (TIME-03)` is present. The audit text for BUG-08 (signed gaps in `long`, uninitialised fields) is consistent with this; the "reported active" claim was the plan's inference.
- **Files modified:** `tests/unit/characterization/test_connection_lifecycle.cpp`
- **Commit:** `8771452`

**2. [Rule 1 - Test design] Grace delay and test names adjusted to observed behaviour**
- **Found during:** Task 1 and Task 2
- **Issue:** The plan says CONNECT leaves "about 10 s" after Wi-Fi connects; the grace delay is measured from boot, so it leaves on the next loop after the WAIT_GRACE transition. A first draft of `BootstrapTimeoutIgnoredOnceConnecting` left the heat pump silent and tripped the natural reconnect at about 14 s.
- **Fix:** Expectations follow the observed timing; the test keeps the link alive by answering polls (`frames_until(..., answer_polls)`).
- **Commit:** `b6c2dac`, `8771452`

**3. [Plan reading] DEFROST and 0x04 error code**
- No DEFROST capture exists (01-12 decision `existing-only`), so no DEFROST test was added and the scenario stays row 4 of Wanted captures. `ErrorInfoDecoded` pins the only real 0x04 frame (`No Error`); a non-zero error needs a real capture, recorded as new row 12 in `tests/unit/README.md`.
- **Commit:** `f938f83`

**Total deviations:** 3 (2 Rule 1 test/expectation fixes, 1 plan reading), none touching `components/cn105`.
**Impact:** none on scope; BUG-08 now documents the real symptom for the Phase 4 fix.

## Issues Encountered

- None blocking. Three first-run failures (BUG-08 expectation, installer window too short, quiet link after connect) fixed in one iteration each.

## Verification Results

- `tests/unit/run-harness.sh --docker --variant all --filter 'ConnectionLifecycle*.*:ReadingsDecode*.*'`: ESP32 21 tests, ESP8266 18 tests, `[run-harness] OK: esp32 esp8266`.
- Full suite: ESP32 132, ESP8266 130, all pass.
- `grep -c 'flip in Phase 5 (LINK-05)'` = 1, `grep -c 'flip in Phase 4 (TIME-03)'` = 1, `grep -c 'IdfRecorder::calls'` = 3 (test_connection_lifecycle.cpp); `grep -c 'checksum corrupted on purpose'` = 1 (test_readings_decode.cpp); no `Characterize_Defrost` test.
- `grep -rn 'Characterize_BUG0[58]_' tests/unit/characterization` lists both new pins.
- `git diff --quiet origin/main -- components/cn105`: clean (D-03).

## User Setup Required

None. One new real capture is wanted (README row 12), not blocking.

## Known Stubs

None.

## Threat Flags

None. T-01-26: the BUG-05 pin comment states it records calls on a host stub and that Phase 5 must verify on a real board first.

## Next Phase Readiness

- Ready for 01-14 (Lossnay). Phase 4 (TIME-03) flips `Characterize_BUG08_ConnectionActiveSignedOverflow` (expected after the fix: positive duration in the log). Phase 5 (LINK-05) flips `Characterize_BUG05_LowLevelReinitTargetsUart0Pins1And2` after the on-board check.

## Self-Check: PASSED

- FOUND: tests/unit/characterization/test_connection_lifecycle.cpp
- FOUND: tests/unit/characterization/test_readings_decode.cpp
- FOUND: commits b6c2dac, 8771452, f938f83
