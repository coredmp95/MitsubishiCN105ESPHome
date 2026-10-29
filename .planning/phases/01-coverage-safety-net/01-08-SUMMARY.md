---
phase: 01-coverage-safety-net
plan: 08
subsystem: testing
tags: [googletest, characterization, hp-writings, pending-packet, bug-03, bug-04, m32, esp32, esp8266]

requires:
  - phase: 01-coverage-safety-net
    provides: "01-01..01-03 fixture API (ClockFixture, Cn105Fixture), real_frames.h, TestClock/TestLog, ESP32 + ESP8266 -m32 harness variants, run-harness.sh"
provides:
  - "HpWritings / HpWritingsAirPurifier / HpWritingsSetFrames / HpWritingsInstaller suites (25 tests per variant)"
  - "Exact UART bytes for every info request code, the CONNECT frames (0x5A and 0x5B) and six 0x41 SET frames compared with real captures"
  - "BUG-03 and BUG-04 pins with their fixing phase named"
affects: [01-12, phase-05-link-stall, phase-06-command-reliability]

actuals:
  tokens: 8400
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "HpWritings::make_connection_inactive() connects, stops answering and steps until the link counts as inactive (exactly 10 * update_interval after the last response)"
    - "Retry timelines are measured with the scheduler only (App.scheduler.call without loop()) when loop() would itself re-enter the connection bootstrap"

key-files:
  created:
    - tests/unit/characterization/test_hp_writings.cpp
  modified: []

key-decisions:
  - "SET frames are produced through the public climate API (and the vertical vane select for the vane-up capture) and compared byte for byte with real_frames::SET_*; all six matched without any difference"
  - "The 'UART not ready' retry is reached with a UART that setupUART() refuses to configure, because no production path returns the FSM below CONNECTING after boot"

patterns-established:
  - "Bug pins are named Characterize_BUG<NN>_... with '// Pins current buggy behaviour — flip in Phase N (REQ-ID)' directly above"
  - "Synthetic frames carry a SYNTHETIC comment naming D-23 and the capture to request"

requirements-completed: [COV-04]

coverage:
  - id: D1
    description: "Exact bytes written for info requests 0x02/0x03/0x04/0x05/0x06/0x09/0x42, the legacy request index mapping, the standard and installer CONNECT frames, and lastSend stamping"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "HpWritings.Characterize_InfoPacketBytesForEachCode, ..._LegacyRequestPacketIndexMapsToInfoCodes, ..._ConnectPacketStandardMode; HpWritingsInstaller.Characterize_InstallerModeConnectUses0x5B (test_hp_writings.cpp)"
        status: pass
    human_judgment: false
  - id: D2
    description: "SET frames built from ClimateCalls equal the real captured 0x41 frames of test_real_frames.cpp (Heat 21.5, FanOnly 19.5, Off 21.5, fan Medium, vane swing, vane up)"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "HpWritingsSetFrames.Characterize_SetFrameMatchesRealCapture_* (6 tests, test_hp_writings.cpp)"
        status: pass
    human_judgment: false
  - id: D3
    description: "writePacket fallback pinned: refused write reconnects at once (CONNECT burst), queues in the single pending slot, retries after 4000 ms; the retry while still inactive reconnects again and clears has_pending_packet_ (packet lost); oversize packets dropped after the reconnect; 2000 ms (+750 ms delay) re-arm while the UART is not ready"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "HpWritings.Characterize_WriteWhenInactiveQueuesAndReconnects, ..._RetryStillInactiveReconnectsAgainAndForgetsPacket, ..._OversizePacketDroppedAfterReconnect, ..._BootWriteBypassesGraceDelayAndConnects, ..._PendingRetriesEvery2sWhileUartNotReady, ..._ConnectionBecomesInactiveAfterTenIntervals"
        status: pass
    human_judgment: false
  - id: D4
    description: "BUG-04 pinned: an info request overwrites the queued user command in the single slot (only the info frame is transmitted after reconnection) and every failed write starts its own reconnect"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "HpWritings.Characterize_BUG04_InfoRequestOverwritesPendingCommand, ..._BUG04_EachFailedWriteTriggersReconnect"
        status: pass
    human_judgment: false
  - id: D5
    description: "BUG-03 pinned: wanted settings and wanted run states are published to HA and cleared although the packet was only queued, and nothing re-sends them"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "HpWritings.Characterize_BUG03_WantedSettingsPublishedAndClearedAfterQueuedWrite, ..._BUG03_NoRetryOfClearedWantedSettings; HpWritingsAirPurifier.Characterize_BUG03_RunStatesPublishedAndClearedAfterQueuedWrite"
        status: pass
    human_judgment: false
  - id: D6
    description: "300 ms send throttle (strict >), synthetic 0x61 ACK is a no-op, installer-mode fallback to 0x5A after 10 s, synthetic 0x7B reply accepted"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "HpWritings.Characterize_SendWantedSettingsThrottle300ms, ..._AckFrame0x61ChangesNothing; HpWritingsInstaller.Characterize_InstallerModeFallsBackTo0x5AAfter10s, ..._InstallerModeAccepts0x7BReply"
        status: pass
    human_judgment: false
  - id: D7
    description: "components/cn105 unchanged"
    requirement: COV-04
    verification:
      - kind: other
        ref: "git diff --quiet origin/main -- components/cn105 (after every task)"
        status: pass
    human_judgment: false

duration: ~40min
completed: 2026-10-29
status: complete
plan_head_before: 0876e19d5c0dc1de8f661e432d4e64a5262ff7b6
plan_head_after: 0a09b4eeeaff44b3db3f9526715ccedbd0ceb703
commits: 3
---

# Phase 1 Plan 08: hp_writings characterization Summary

**The write path of the unmodified component is pinned with exact UART bytes on both the ESP32 and ESP8266 32-bit variants: info requests, CONNECT (0x5A/0x5B), six SET frames equal to real captures, the single pending-packet slot with its retry timeline, and the BUG-03 / BUG-04 pins that Phases 5 and 6 will flip deliberately.**

## Performance

- **Duration:** ~40 min (start time not captured; estimated)
- **Completed:** 2026-10-29
- **Tasks:** 3
- **Files:** 1 created (test file only)

## Accomplishments

- 25 tests per variant in 4 suites (`HpWritings` 15, `HpWritingsAirPurifier` 1, `HpWritingsSetFrames` 6, `HpWritingsInstaller` 3). Full harness run: ESP32 70/70, ESP8266 69/69.
- All six SET frames built through the climate API (and the vertical vane select for vane up) equal `real_frames::SET_*` plus checksum with no difference to report.
- BUG-04 and BUG-03 pins carry `// Pins current buggy behaviour — flip in Phase N (REQ-ID)` comments: 5 flip comments (LINK-01, LINK-02, CMD-04 x3).

## Task Commits

1. **Task 1 (tracer): connected info-request bytes and CONNECT** - `0f9b5c6` (test)
2. **Task 2: pending slot, reconnect bursts, BUG-04 and BUG-03** - `f093e47` (test)
3. **Task 3: SET frames vs real captures, throttle, ACK, installer handshake** - `0a09b4e` (test)

Tracer gate (automated-only verify, end-of-phase mode): Task 1 verify re-run end to end on both variants (`[run-harness] OK`) before expanding.

## Observed values worth knowing (pinned)

- Link goes inactive exactly 10000 ms after the last response (`< 10 * update_interval`); the FSM stays CONNECTED until a write is attempted. A write attempted then triggers `reconnectUART()` and a CONNECT burst; the SET packet goes into `pending_packet_` and the "write" timeout fires 4000 ms later.
- At that retry the link is still inactive (no CONNECT reply), so `writePacket()` fails again, reconnects again and re-queues, and then `try_write_pending_packet()` sets `has_pending_packet_ = false` unconditionally: the SET frame never reaches the wire without a heat pump reply (`Characterize_RetryStillInactiveReconnectsAgainAndForgetsPacket`). Not in the audit's BUG list as such; it belongs to the LINK-02 rework.
- A write attempted during BOOT (before the grace delay) brings the UART up and sends CONNECT immediately.
- The info request that replaces a queued SET is retransmitted about 3980 ms after the link comes back, distinct from the regular poll requests (about +3040 and +7060 ms).
- `sendWantedSettings` throttle: `millis() - lastSend > 300` is strict (deferred at exactly 300 ms, sent at 301).
- A checksum-valid 0x61 frame advances `lastResponseMs` (any valid frame counts as a response for the watchdog) but changes nothing else.
- Installer mode: CONNECT is `FC 5B 01 30 02 CA 01 A7`; with no reply the fallback to `FC 5A ... A8` goes out 10000-10020 ms after the first CONNECT.
- `sendWantedRunStates()` queued with the link down still flips the HA switch to ON while `currentRunStates.air_purifier` stays at the reset value -1.

## Deviations from Plan

### Plan reading contradicted by observation (characterization rule: assert observed, comment, report)

**1. [Observation] "UART not ready" cannot be forced through disconnectUART()**
- **Found during:** Task 2
- **Issue:** The plan says to force the UART below CONNECTING through `disconnectUART()`. `setHeatpumpConnected(false)` only moves CONNECTED to DISCONNECTED, and DISCONNECTED compares as ready (`state_ >= CONNECTING`). No production path returns the FSM to BOOT/WAIT_* after boot.
- **Fix:** `Characterize_PendingRetriesEvery2sWhileUartNotReady` makes `setupUART()` refuse the UART (parity changed after SetUp), so the state never leaves BOOT; documented in the test. The retry timeline is measured with the scheduler only because `loop()` would re-enter the bootstrap each iteration. Observed: first retry ~4000 ms after the write, then every 2000 ms plus the 750 ms `CUSTOM_DELAY` of each attempt (gap 2750-2780 ms).
- **Files modified:** `tests/unit/characterization/test_hp_writings.cpp`

**2. [Observation] Retry while still inactive is not "another queue + reconnect cycle"**
- **Found during:** Task 2
- **Issue:** Plan expected another cycle; the code re-queues but then clears `has_pending_packet_`, so the cycle ends.
- **Fix:** Pinned as observed (see above).

**Total deviations:** 2 observations, 0 production changes. **Impact:** none on scope; both strengthen the Phase 5 target description.

## Real captures needed

- **0x61 ACK after a 0x41 SET** (SYNTHETIC in `Characterize_AckFrame0x61ChangesNothing`, D-23): needed to confirm the payload and that the ACK should (Phase 6, CMD-04) confirm a queued command. Scenario: send one SET (e.g. mode/temperature change from HA) and capture the next frames on the bus.
- **0x7B installer-mode reply** (SYNTHETIC in `Characterize_InstallerModeAccepts0x7BReply`, D-23): scenario: a unit that accepts `installer_mode: true`, capture the reply to `FC 5B ...`. Both are collected for plan 01-12.
- Not covered and not required here: a real 0x42 (HVAC options) response (already listed in 01-07) and a real remote-temperature SET flow (`real_frames::SET_REMOTE_TEMP_22_8` exists but belongs to another plan).

## Issues Encountered

None. An early attempt to create the test file in one shell command failed silently (a `mkdir` guard short-circuited the heredoc); caught by the harness reporting 0 selected tests and redone with the Write tool.

## Verification Results

- `tests/unit/run-harness.sh --docker --variant all --filter 'HpWritings*'`: 25 tests per variant, `[run-harness] OK: esp32 esp8266`.
- `tests/unit/run-harness.sh --docker --variant all`: ESP32 70/70, ESP8266 69/69.
- Acceptance greps: `Characterize_BUG04_` x2, `Characterize_BUG03_` x3, `flip in Phase (5 (LINK-0[12])|6 (CMD-04))` x5, `SYNTHETIC` x2, `real_frames::SET_` x7.
- `git diff --quiet origin/main -- components/cn105`: clean after every task (D-03).

## User Setup Required

None.

## Known Stubs

None.

## Threat Flags

None. T-01-15 (SYNTHETIC labelling and capture list above) and T-01-16 (no production edit) mitigated as verified.

## Next Phase Readiness

- Phase 5 flips `Characterize_BUG04_*` (LINK-01/LINK-02) and revisits `Characterize_RetryStillInactiveReconnectsAgainAndForgetsPacket`; Phase 6 flips the three `Characterize_BUG03_*` pins (CMD-04).

## Self-Check: PASSED

- File exists: `tests/unit/characterization/test_hp_writings.cpp`.
- Commits `0f9b5c6`, `f093e47`, `0a09b4e` are ancestors of HEAD.
