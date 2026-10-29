# Unit tests, characterization harness and coverage

Everything under `tests/unit/` is built without ESPHome firmware tooling. Two test families share one
CMake project (`tests/unit/CMakeLists.txt`), and one script measures both.

## The two test families

| Family | What it is | Build |
|---|---|---|
| Pure tests (`test_*.cpp` in this directory) | GoogleTest on the pure protocol code (`cn105_protocol.h`, `frame_parser.h`, the profile protocol headers), compiled against the hand-written stubs in `mocks/`. Fast. | 64-bit |
| Characterization harness (`characterization/`, `harness/`) | The real `CN105Climate` and the real ESPHome 2026.10.0 core (fetched by CMake, never patched), driven through a fake UART and a test clock. One binary per platform variant: ESP32 and ESP8266. Pins today's behaviour, bugs included, so that later refactors cannot change it silently. | `-m32` |

The harness is built with `-m32` on purpose: on the target chips `unsigned long` is 4 bytes, and the
millis() wrap-around bugs only reproduce with 32-bit arithmetic. A host without `g++-multilib` uses
the Docker image (`tests/unit/docker/run.sh`, ubuntu 24.04 + g++-multilib) through the `--docker` flag.

## Quick start

```bash
# Fast pure tests (64-bit, no Docker)
cmake -B tests/unit/build -S tests/unit && cmake --build tests/unit/build && ctest --test-dir tests/unit/build

# Characterization harness (both variants); --filter takes a GoogleTest filter
tests/unit/run-harness.sh --docker
tests/unit/run-harness.sh --docker --filter 'CycleManagement.*'

# Everything, with line + branch coverage: the one command that reproduces the CI report
tests/unit/coverage.sh --docker
```

`coverage.sh` flags: `--docker` (run in the dev image), `--ci` (CI mode: no virtualenv bootstrap, no Docker
re-exec, `gcovr` and `diff-cover` already on `PATH`), `--gate` (exit 5 when the ratchet verdict is FAIL),
`--check-scope`, `--build-dir`, `--out`, `--help`. Exit codes: 0 ok, 1 build or test failure, 2 environment
problem, 3 coverage scope failure, 5 ratchet FAIL with `--gate`.

## Reading the reports

`coverage.sh` writes to `tests/unit/out/` (git-ignored). CI uploads the same directory as the
`coverage-report` artifact and appends `report.md` to the job summary.

- `report.md`: overall line, function and branch coverage, the scope verdict, the ratchet verdict.
- `html/index.html`: per-file, per-line drill-down.
- `coverage.xml`: Cobertura, the input of `diff-cover`.
- `summary.json`, `gate.md`, `scope.md`, `toolchain.json`: inputs and verdicts of `tools/coverage_gate.py`.

## Coverage policy

- Coverage is measured on line and branch over every `.cpp` and `.h` of `components/cn105/`, with no
  exclusion (no `GCOVR_EXCL`, no filtered directory).
- The committed `tests/unit/coverage-baseline.json` is the floor. A run fails when line or branch coverage
  falls more than **0.1 point** below it. A rise beyond 0.1 point is reported as `RATCHET`: raise the
  baseline.
- The baseline only goes up. A lower baseline is accepted only when the recorded toolchain (GCC, gcovr,
  ESPHome, OS) changed in the same pull request.
- Coverage depends on the GCC version, so the numbers that count are the ones produced on the pinned
  `ubuntu-24.04` runner and in the Docker image, not those of a native build with another compiler.
- **Diff coverage:** on a pull request, at least **80%** of the changed, coverable lines of
  `components/cn105/` must be covered (`diff-cover` against the base branch).
- `tests/unit/tools/coverage_gate.py` holds this logic as pure Python. Its self-test
  (`python3 -m unittest discover -s tests/unit/tools`) runs in CI on every run, so the "a drop fails,
  a rise ratchets" demonstration is re-proven each time.

### How CI reports and enforces it

- `unit-tests` job: 64-bit pure tests, then `tests/unit/coverage.sh --ci`; report in the job summary and
  the `coverage-report` artifact.
- `coverage-gate` job: gate self-test, ratchet, diff coverage on pull requests, and one pull request
  comment (marker `<!-- cn105-coverage-report -->`) that is updated in place. A pull request from a fork
  gets no comment (read-only token); read the job summary instead.
- The coverage data stays in GitHub: no third-party service, no third-party action, and CI never commits
  or pushes anything.

### How to ratchet the baseline

1. Take the proposed JSON from the `coverage-gate` job summary or the PR comment (block "Commit this as
   tests/unit/coverage-baseline.json"). Alternatively download the `coverage-report` artifact and run
   `python3 tests/unit/tools/coverage_gate.py propose --summary <dir>/summary.json --toolchain <dir>/toolchain.json --source "<where it came from>"`.
2. Never take the numbers from a local native build with another GCC.
3. Commit the file in the pull request. Review shows exactly which numbers moved.

The baseline is `provisional: true` with `0.0` until the first `ubuntu-24.04` CI run replaces it.

### How to use the exemption

If the changed lines cannot reasonably be covered (for example a hardware-only branch), a maintainer adds
the `coverage-exempt` label to the pull request and writes the justification in the PR description, then
re-runs the failed jobs (the label is read live at run time). The label never hides the measurement: the
job summary and the comment still show the measured diff coverage, marked `EXEMPT`.

## Characterization conventions

- One file per module under `characterization/` (`test_hp_writings.cpp`, `test_request_scheduler.cpp`, ...),
  all based on the `Cn105Fixture` class in `characterization/fixture.h`.
- A test that pins a known defect is named `Characterize_BUG<NN>_<what>` and carries a comment that says
  what the fixed behaviour will be, so the phase that fixes the bug flips the assertion and renames the
  test. `NN` is the number in the 2026-10-27 audit.
- Frames come from real captures (`characterization/real_frames.h`). Do not invent frames. The only
  exceptions are explicit maintainer decisions (D-23): the `0x61` ACK and the `0x7B` installer reply are
  labelled `SYNTHETIC` in the tests until real captures replace them. When a scenario needs a capture
  that does not exist, ask the maintainer for it and say which scenario it covers.
- `deferCycle()` is pinned at 1500 ms, not 750 ms, because the harness runs at log level VERY_VERBOSE and
  the INFO-level branch is not compiled in.

## Wanted captures

Characterization never invents a frame (see above). These real CN105 captures do not exist in the
repository yet; until they do, the paths below stay unpinned and the two D-23 synthetic frames stay in place.
Maintainer decision for PR 1b: proceed with the existing frames only. When a capture arrives, add it to
`characterization/captured_frames_1b.h` (frame without checksum, a comment with the capture file, date and
scenario), write the test that feeds it, and delete the matching line here.

Capture with the sniffer on the fork: `examples/sniffer/cn105-sniffer.yaml`, `tools/cn105_capture.py`,
`docs/sniffing.md`. For each scenario, record between the official module (or this component) and the indoor
unit.

| # | Scenario to capture | How | Unblocks |
|---|---|---|---|
| 1 | `0x42` HVAC-options reply from a unit that has air purifier, night mode and circulator | Toggle each option once from the remote or the app, capture the reply to the `FC 42 ... 42` info request after each toggle | `hvac_options` decode (`decode_hvac_options`, the option switches) in PR 1b; Phase 6 (CMD-04) |
| 2 | `0x20` and `0x22` functions replies | Enable `installer_mode`, press the "get functions" button once, then change one hardware setting and read the functions again | Functions decode and the `FunctionsNumber` / hardware select publish paths, PR 1b |
| 3 | `0x61` ACK after a `0x41` SET (mode change) and after a `0x41`/`0x07` remote-temperature packet | Change the mode from HA once; push one remote temperature; capture the unit's answer to each write | Replaces the synthetic ACK in `HpWritings.Characterize_AckFrame0x61ChangesNothing` (D-23); the real ACK after `0x07` for the remote-temperature suite; Phase 6 (CMD-02) |
| 4 | `0x09` reply while the unit defrosts (DEFROST sub-mode) | Capture the `0x09` poll during a defrost cycle (outdoor unit at low temperature, HEAT running a while) | Sub-mode DEFROST decode in `ProfileState*`, PR 1b |
| 5 | `0x7B` installer-mode connect reply to a `0x5B` CONNECT | Set `installer_mode: true`, power-cycle the module and capture the reply to `FC 5B 01 30 02 CA 01` (expected `FC 7B 01 30 ...`). If the unit ignores `0x5B` and the component falls back to `0x5A` after 10 s, capture that too | Replaces the synthetic reply in `HpWritingsInstaller.Characterize_InstallerModeAccepts0x7BReply` (D-23); the fallback pin |
| 6 | Lossnay handshake reply to CONNECT `0x5A` with profile `0x34` | On a Lossnay unit capture the reply to `FC 5A 01 34 02 CA 01 A4` (expected `FC 7A 01 34 ...`) | `ProfileStateLossnay` can connect through a real frame instead of the `handleConnectionSuccess()` shortcut; the Lossnay plan 01-14 |

Extra wishes recorded by earlier plans:

| # | Scenario to capture | How | Unblocks |
|---|---|---|---|
| 7 | A `0x41` SET carrying a `wideVane` change on a unit with horizontal vanes (swing HORIZONTAL / BOTH) | Change the horizontal vane from the remote or HA and capture the write | Horizontal swing encode in `ClimateControls*` (plan 01-09), Phase 7 |
| 8 | A `0x41` SET in Fahrenheit mode | Switch the module to a Fahrenheit option, change the target temperature once and capture the write | Fahrenheit encode of commands (plan 01-09) |
| 9 | Lossnay `0x03`, `0x06` and `0x04` info replies | On a Lossnay unit capture the replies to those three requests. Today an unanswered `0x03` ends every Lossnay cycle by timeout after 3 s, so the `0x09` captures are only fed as the answer to `0x03` | Natural Lossnay cycle and power/energy decode in `ProfileStateLossnay` (plan 01-10), Phase 8 |
| 10 | A heat-pump `0x06` reply with the operating byte set (compressor running, HEAT) | Capture the `0x06` poll while the compressor runs in HEAT. Only `STATUS_IDLE` exists today | HEATING / COOLING action without the stage fallback (plan 01-10) |
| 11 | A heat-pump `0x02` reply in AUTO mode (mode byte `0x08`) and one with a HEAT_COOL-compatible state | Put the unit in AUTO, capture the `0x02` poll; repeat with a HEAT_COOL band set from HA | Real-frame reconciliation of a HEAT_COOL band after a poll (`restore_setpoints()`, plan 01-10) |
| 12 | A `0x04` error-info reply carrying a real error code (code and sub-code non-zero) | While the unit reports a fault (or from its error history), capture the reply to the `FC 42 ... 04` info request; only the `0x80` "No Error" reply exists today | Decode of a non-zero `Error 0x.. sub 0x..` through `ReadingsDecodeSensors` (plan 01-13) |

## How headers are counted

A header with inline code only produces object code where something includes it. To keep header code
visible in the report (D-10), every `components/cn105` header is compiled in a generated coverage-only
translation unit with `-fkeep-inline-functions`. Consequences:

- inline code of a header counts as uncovered even when no test calls it;
- a header with no executable line (declarations or constants only) is listed as `0/0` under "no executable
  lines" and does not change the percentages;
- template code counts only once something instantiates it;
- a `.cpp` or `.h` of `components/cn105` that is absent from the report fails `coverage.sh` with exit 3
  (the scope check), so a new file can never hide from coverage.

## Bumping ESPHome

The harness uses the ESPHome release that the CI `stable` image uses today.

1. Edit `GIT_TAG` and the expected commit (`CN105_ESPHOME_TAG`, `CN105_ESPHOME_COMMIT`) in
   `tests/unit/harness/CMakeLists.txt`. The commit check is a supply-chain guard: take the commit from the
   release tag itself.
2. Update the version overlay `tests/unit/harness/overlay/esphome/core/version.h`.
3. Confirm that the CI `esphome-build` image (`ghcr.io/esphome/esphome:stable`) is the same version.
4. Rebuild with `tests/unit/coverage.sh --docker`. Expect small shim edits under `harness/shim_*` and
   `harness/platform`.
5. Record the new toolchain in `tests/unit/coverage-baseline.json` (the ratchet job warns when the
   toolchain differs from the recorded one).
