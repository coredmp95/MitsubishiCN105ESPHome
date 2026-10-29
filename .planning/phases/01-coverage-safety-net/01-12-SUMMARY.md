---
phase: 01-coverage-safety-net
plan: 12
subsystem: testing
tags: [googletest, characterization, remote-temperature, bug-02, captures, m32, esp32, esp8266]

requires:
  - phase: 01-coverage-safety-net
    provides: "01-01..01-03 fixture API (Cn105Fixture, run_poll_cycle, take_tx_frames, TestLog), real_frames.h (SET_REMOTE_TEMP_22_8, ROOM_TEMP_23C), ESP32 + ESP8266 -m32 harness variants; 01-08 D-23 synthetic frames; 01-10 Wanted captures list"
provides:
  - "RemoteTemperature suites: 10 tests per variant, identical on ESP32 and ESP8266"
  - "The remote-temperature packet pinned byte for byte against the real capture SET_REMOTE_TEMP_22_8 through set_remote_temperature()"
  - "Keep-alive resend, same-value debounce, timeout fallback frame, source-info text sensor, profile acceptance and Lossnay refusal pinned"
  - "Characterize_BUG02_MarginUsesIntegerAbs (flip in Phase 4, TIME-04) plus its 1.2 C companion"
  - "captured_frames_1b.h (namespace cn105_test::captured_frames), intentionally empty, and a Wanted captures section in tests/unit/README.md listing 11 scenarios"
affects: [01-13, 01-14, phase-04-time, phase-06-command-reliability]

actuals:
  tokens: 6000
  tasks: 3
  commits: 2

tech-stack:
  added: []
  patterns:
    - "RemoteTemperatureEntities fixture subclass attaches the remote-temperature options in before_setup() with the production setters (sensor, margin, keep-alive, timeout) and exposes remote_frames_over(ms), which returns every 0x41/0x07 frame with its test-clock time"
    - "Entities are real ESPHome objects (BinarySensor, Sensor, RemoteTempSourceInfo); a Sensor gets its name through configure_entity_() (reachable with -fno-access-control)"
    - "Timing of an ESPHome interval is pinned by spacing, never by absolute time: the scheduler staggers a new interval by a variant-dependent offset"

key-files:
  created:
    - tests/unit/characterization/captured_frames_1b.h
  modified:
    - tests/unit/characterization/test_remote_temperature.cpp
    - tests/unit/README.md

key-decisions:
  - "Task 2 decision: existing-only. No new captures supplied; the two D-23 synthetic frames (0x61 ACK, 0x7B) stay in test_hp_writings.cpp labelled SYNTHETIC; no capture-dependent frame was invented; captured_frames_1b.h stays empty and documents the six wanted scenarios"
  - "All six PR 1b scenarios plus five extra wishes from earlier SUMMARYs (01-09, 01-10) are recorded under Wanted captures in tests/unit/README.md, each with scenario, how to capture and what it unblocks"
  - "Keep-alive timing is asserted as spacing (first resend 10-26 s after the initial send, then 18-23 s apart), because the first keep-alive tick is staggered by a random offset that differs between variants"

patterns-established:
  - "Plans without captures leave an empty, documented home (captured_frames_1b.h) and a README list instead of synthetic frames"

requirements-completed: [COV-04]

coverage:
  - id: D1
    description: "The remote-temperature packet produced by set_remote_temperature(22.8) equals the real capture SET_REMOTE_TEMP_22_8 byte for byte"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "RemoteTemperature.Characterize_RemoteTempPacketMatchesRealCapture (tests/unit/characterization/test_remote_temperature.cpp)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Keep-alive resend of an unchanged value, same-value debounce (log and skip counter, changed value never debounced), timeout fallback frame to the internal sensor"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "RemoteTemperatureEntities.Characterize_KeepAliveResendsAfterInterval, Characterize_SameValueDebounced, RemoteTemperatureShortTimeout.Characterize_TimeoutFallsBackToInternal"
        status: pass
    human_judgment: false
  - id: D3
    description: "Source-info text sensor, profile acceptance range (NaN only rejected) and Lossnay refusal (value dropped, warning, nothing written)"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "RemoteTemperatureSource.Characterize_SourceInfoTextSensor, RemoteTemperatureSourceReversed.Characterize_SourceInfoNotPublishedWhenSetBeforeSource, RemoteTemperatureEntities.Characterize_ProfileAcceptsRemoteTemperatureRange, RemoteTemperatureLossnay.Characterize_LossnayRefusesRemoteTemperature"
        status: pass
    human_judgment: false
  - id: D4
    description: "BUG-02 pinned: a 0.6 C gap with a 0.4 C margin reports the remote temperature in use (integer abs); a 1.2 C gap is detected, an exact match is remote"
    requirement: COV-04
    verification:
      - kind: unit
        ref: "RemoteTemperatureEntities.Characterize_BUG02_MarginUsesIntegerAbs, Characterize_BUG02_GapAboveOneDegreeIsDetected"
        status: pass
    human_judgment: false
  - id: D5
    description: "Real captures needed for PR 1b flagged to the maintainer, decision recorded, every missing scenario tracked in Wanted captures, no invented frame, components/cn105 unchanged"
    requirement: COV-04
    verification:
      - kind: other
        ref: "grep -n '^## Wanted captures' tests/unit/README.md; git diff --quiet origin/main -- components/cn105"
        status: pass
    human_judgment: false

duration: ~60min
completed: 2026-10-29
status: complete
plan_head_before: 6911e8be6131aee8e5ba97667df1af27c0654be4
plan_head_after: 83854c98205060fd321ea5580cf0e293fdc322ad
commits: 2
---

# Phase 1 Plan 12: Remote-temperature characterization and wanted captures Summary

**The remote-temperature path (real-capture packet, keep-alive, debounce, timeout fallback, source-info text, profile acceptance, Lossnay refusal) and the BUG-02 integer-abs margin quirk are pinned on both variants, and the 11 real captures still missing are listed in tests/unit/README.md instead of being invented.**

## Performance

- **Duration:** ~60 min across the original run and this continuation (start time not captured; estimated)
- **Completed:** 2026-10-29
- **Tasks:** 3 (1 tracer, 1 decision checkpoint, 1 auto)
- **Files:** 1 created (`captured_frames_1b.h`), 2 modified (`test_remote_temperature.cpp`, `README.md`); `test_hp_writings.cpp` untouched

## Accomplishments

- 10 tests selected by `RemoteTemperature*` on both variants; full harness run: ESP32 111/111, ESP8266 112/112.
- Tracer gate (automated-only verify, end-of-phase mode): Task 1 verify passed on both variants before expanding.
- `components/cn105` unchanged (`git diff --quiet origin/main -- components/cn105` clean, D-03). No new synthetic frame.

## Task Commits

1. **Task 1 (tracer): remote-temperature packet against the real capture** - `c50198d` (test)
2. **Task 2: capture decision checkpoint** - no commit; answered by the maintainer (see Decision record)
3. **Task 3: remote-temperature catalog, BUG-02 pin, captures home, Wanted captures** - `83854c9` (test)

## Decision record (Task 2, blocking-human)

Maintainer answer: **`existing-only`** - proceed with existing frames only. No new captures supplied. Consequences applied:

- The two D-23 synthetic frames (`0x61` ACK in `HpWritings.Characterize_AckFrame0x61ChangesNothing`, `0x7B` reply in `HpWritingsInstaller.Characterize_InstallerModeAccepts0x7BReply`) stay in `test_hp_writings.cpp` with their `SYNTHETIC` label. **Replaced by a capture: none.**
- `captured_frames_1b.h` stays empty (comment lists the six scenarios) and is included by `test_remote_temperature.cpp` so it is compiled.
- No test was added for the real `0x61` ACK after `0x07`, nor for any capture-dependent decode path (0x42, 0x20/0x22, DEFROST, installer handshake, Lossnay handshake).

## Scenarios still wanted (all recorded in `tests/unit/README.md`, section "Wanted captures")

PR 1b scenarios:

1. `0x42` HVAC-options reply (air purifier / night mode / circulator, each option toggled once)
2. `0x20` and `0x22` functions replies in installer mode
3. `0x61` ACK after a `0x41` SET (mode change) and after the `0x41`/`0x07` remote-temperature packet (replaces the D-23 synthetic ACK; Phase 6 CMD-02)
4. `0x09` reply while the unit defrosts (DEFROST sub-mode)
5. `0x7B` installer-mode connect reply to `0x5B` (replaces the D-23 synthetic reply), or the 10 s fallback to `0x5A`
6. Lossnay handshake reply to CONNECT `0x5A` with profile `0x34` (`FC 7A 01 34 ...`)

Extra wishes consolidated from earlier plans:

7. (01-09) `0x41` SET carrying a `wideVane` change on a horizontal-vane unit (swing HORIZONTAL / BOTH)
8. (01-09) `0x41` SET in Fahrenheit mode
9. (01-10) Lossnay `0x03` / `0x06` / `0x04` info replies
10. (01-10) Heat-pump `0x06` reply with the operating byte set (compressor running, HEAT)
11. (01-10) Heat-pump `0x02` reply in AUTO mode (mode byte `0x08`) and one with a HEAT_COOL-compatible state

## Observations pinned (asserted as observed)

- **BUG-02 reproduces on host.** `getRoomTemperatureFromResponsePacket()` computes `abs(float - float)` with the integer overload on both variants: with room 23.0 C (ROOM_TEMP_23C), remote 22.4 C and margin 0.4 C the "remote in use" binary sensor is `true` (gap 0.6 truncated to 0). Remote 21.8 C (gap 1.2, truncated to 1) gives `false`, remote 23.0 C gives `true`. Pin flips in Phase 4 (TIME-04); the expected fixed value for 0.6 C is `false`.
- **Keep-alive.** The first keep-alive tick is staggered by ESPHome's random interval offset (0-5 s, deterministic in the test platform but different per variant). A tick within 10 s of the previous send is swallowed by the debounce (`min_interval = max(keep-alive / 2, 5 s)`); later ticks are 20 s apart and re-send the identical packet at the end of the next poll cycle. Over 50 s: 3 identical frames.
- **Debounce.** An identical value 3 s after the send writes nothing, logs `Debounce: skipping remote temp send`, increments `remote_temp_debounce_skip_count_`, and still consumes `shouldSendExternalTemperature_`. A changed value is sent at once and resets the counter.
- **Timeout fallback.** After the timeout without a new sample, `set_remote_temperature(0)` stops the keep-alive and one last `0x41`/`0x07` packet goes out with byte 6 (enable) = `0x00`; `remoteTemperature_` is 0. The "remote in use" sensor reads `false`.
- **Source info text sensor.** Published once at setup with the NAME of the source sensor (`Hallway thermometer`); no other event writes to it, so "remote" and "internal" are not distinct strings. With the opposite setter order (info sensor before source) nothing is ever published. A source reading is forwarded as the remote temperature; NaN is dropped.
- **Profile acceptance.** `accepts_remote_temperature` rejects only NaN (warning `Remote temperature is NaN, ignoring.`). 0 and negative values are accepted and stored (no keep-alive); 100 C is accepted and starts the keep-alive. There is no range check.
- **Lossnay.** `set_remote_temperature` is refused through `allow_operation()` (warning `Ignoring unsupported Lossnay operation`), nothing is stored and `sendRemoteTemperaturePacket()` writes nothing when called directly.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Test design] Keep-alive timing asserted as spacing, not as absolute time**
- **Found during:** Task 3 first run (ESP32 variant)
- **Issue:** The plan asked to "pin the resend time of an unchanged value". The first draft expected a resend 20 s after the first send; the real log showed the scheduler staggering the interval by 4611 ms on ESP32, so the first tick was debounced and only 2 frames appeared in 45 s. The offset differs between variants.
- **Fix:** Window extended to 50 s and the spacing pinned (first resend 10-26 s after the initial send, next 18-23 s later), with a comment explaining the stagger and the debounce window.
- **Files modified:** `tests/unit/characterization/test_remote_temperature.cpp`
- **Commit:** `83854c9`

**2. [Interface adjustment] Source-info text: only the source name is ever published**
- **Issue:** The plan lists "published strings for remote and internal". The component has a single string (the source sensor name), published once at setup. Both aspects are pinned as observed, plus the setter-order quirk, instead of two invented strings.
- **Commit:** `83854c9`

**3. [Note] Fixture sets the remote timeout explicitly**
- `remote_temp_timeout_` is uninitialised in the constructor (audit BUG-08) and the zero-filled storage makes it 0 ms, which would fire the watchdog immediately. Production always sets it from YAML, so `RemoteTemperatureEntities::before_setup` does too. No production change.

**Total deviations:** 3 (1 test-design fix, 2 plan-reading adjustments), none touching `components/cn105`.

## Issues Encountered

- None blocking. One test failed on the first run (keep-alive count, see Deviation 1) and was fixed in one iteration.

## Verification Results

- `tests/unit/run-harness.sh --docker --variant all --filter 'RemoteTemperature*'`: 10 tests per variant, `[run-harness] OK: esp32 esp8266`.
- Full suite: ESP32 111, ESP8266 112, all pass, `[run-harness] OK: esp32 esp8266`.
- `grep -n '^## Wanted captures' tests/unit/README.md`: present. `grep -c 'flip in Phase 4 (TIME-04)' test_remote_temperature.cpp` = 1.
- `git diff --quiet origin/main -- components/cn105`: clean (D-03).
- `grep -n SYNTHETIC tests/unit/characterization/test_hp_writings.cpp`: both D-23 frames still labelled (none supplied).

## User Setup Required

None. Real captures are requested through the Wanted captures list (maintainer action, not blocking).

## Known Stubs

None.

## Threat Flags

None. `captured_frames_1b.h` holds no frame (T-01-24: provenance comment required for every future constant).

## Next Phase Readiness

- Ready for 01-13 and 01-14. Plan 01-14 (Lossnay) can use scenario 6 once supplied; until then it keeps the `handleConnectionSuccess()` shortcut.
- Phase 4 (TIME-04) flips `Characterize_BUG02_MarginUsesIntegerAbs` (and its keep-alive/timeout pins if the fallback changes). Phase 6 needs scenario 3 (real ACKs).

## Self-Check: PASSED

- FOUND: tests/unit/characterization/test_remote_temperature.cpp
- FOUND: tests/unit/characterization/captured_frames_1b.h
- FOUND: tests/unit/README.md section "Wanted captures"
- FOUND: commits c50198d, 83854c9
