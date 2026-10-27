---
phase: "1"
slug: "coverage-safety-net"
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
# audit-milestone §5.5 distinguishes NOT-VALIDATED (draft) from PARTIAL (validated + nyquist_compliant: false) (#2117)
status: draft
nyquist_compliant: false
wave_0_complete: false
created: "2026-10-27"
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GoogleTest v1.15.2 (+ first `TEST_F` fixture); Python `unittest` for the coverage gate |
| **Config file** | `tests/unit/CMakeLists.txt` (+ new `tests/unit/harness/CMakeLists.txt`) — Wave 0 adds the harness |
| **Quick run command** | `ctest --test-dir tests/unit/build-cov -R 'CycleManagement|RequestScheduler|HpWritings|ClimateControls|ProfileState' --output-on-failure` |
| **Full suite command** | `tests/unit/coverage.sh` (CI: `tests/unit/coverage.sh --ci`) |
| **Gate self-test** | `python3 -m unittest discover -s tests/unit/tools -v` |
| **Estimated runtime** | ~120 seconds (full, all variants); quick < 15 s |

---

## Sampling Rate

- **After every task commit:** Run the affected module's gtest filter on the ESP32 variant, or `python3 -m unittest discover -s tests/unit/tools` for gate work
- **After every plan wave:** Run `tests/unit/coverage.sh` + gate self-test
- **Before `/gsd-verify-work`:** Full suite must be green; CI `unit-tests`, `coverage-gate`, `esphome-build` 7/7 green
- **Max feedback latency:** 120 seconds

---

## Per-Task Verification Map

| Req ID | Behavior | Test Type | Automated Command | File Exists | Status |
|--------|----------|-----------|-------------------|-------------|--------|
| COV-01 | Line + branch coverage of `components/cn105/` published (summary + artifact); one local command | script smoke + CI | `tests/unit/coverage.sh && test -s tests/unit/out/summary.json && test -s tests/unit/out/html/index.html` | ❌ W0 | ⬜ pending |
| COV-01 | Every `components/cn105/*.cpp` and `*.h` appears in the report (D-10; declaration-only headers listed 0/0) | script check | `tests/unit/coverage.sh --check-scope` | ❌ W0 | ⬜ pending |
| COV-02 | Drop > 0.1 pt fails, rise proposes ratchet (D-14) | unit (Python) | `python3 -m unittest discover -s tests/unit/tools -v` | ❌ W0 | ⬜ pending |
| COV-02 | Baseline file present, parsable, compared | script check | `python3 tests/unit/tools/coverage_gate.py --summary tests/unit/out/summary.json --baseline tests/unit/coverage-baseline.json` | ❌ W0 | ⬜ pending |
| COV-03 | Changed-line coverage reported, 80 % blocking, `coverage-exempt` bypass | CI integration | PR run of `coverage-gate` job; local: `diff-cover tests/unit/out/coverage.xml --compare-branch=origin/main` | ❌ W0 | ⬜ pending |
| COV-04 | cycle_management characterization | unit (gtest) | `--gtest_filter='CycleManagement.*'` | ❌ W0 | ⬜ pending |
| COV-04 | request_scheduler characterization | unit (gtest) | `--gtest_filter='RequestScheduler.*'` | ❌ W0 | ⬜ pending |
| COV-04 | hp_writings / writePacket / pending packet | unit (gtest) | `--gtest_filter='HpWritings.*'` | ❌ W0 | ⬜ pending |
| COV-04 | climateControls (ESP32 + ESP8266) | unit (gtest, 2 variants) | `--gtest_filter='ClimateControls.*'` on both binaries | ❌ W0 | ⬜ pending |
| COV-04 | Profile state (HeatPump + Lossnay) | unit (gtest) | `--gtest_filter='ProfileState.*'` | ❌ W0 | ⬜ pending |
| Gate 5 | Existing tests pass; 7 configs compile; no shipped behaviour change | regression | full `ctest` + `git diff --stat origin/main -- components/` (expect empty) + CI `esphome-build` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/unit/harness/` — FetchContent ESPHome 2026.10.0, overlay (`defines.h`, `version.h`, `esphome.h`), ESP32/ESP8266 shims, platform stubs, test clock, log capture, in-memory preferences; `-m32`, `-fno-access-control` on test TUs
- [ ] `tests/unit/characterization/fixture.{h,cpp}` + smoke test (CONNECT bytes, handshake, one poll cycle) on ESP32 + ESP8266
- [ ] `tests/unit/coverage.sh`, `tests/unit/requirements-coverage.txt`, `tests/unit/tools/coverage_gate.py` + `test_coverage_gate.py`, `tests/unit/coverage-baseline.json`
- [ ] `.github/workflows/build.yaml`: `ubuntu-24.04`, `g++-multilib`, coverage + gate jobs, artifact, sticky PR comment
- [ ] `.gitignore`: `tests/unit/build-cov*/`, `tests/unit/out/`

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| PR comment updated in place, `coverage-exempt` label bypass | COV-03 | Needs a real PR on the fork with `GITHUB_TOKEN` | Open the PR on `coredmp95/MitsubishiCN105ESPHome`, push twice, confirm one comment edited; add label, confirm gate reports but passes |
| Baseline taken from first CI run | COV-02 | Depends on CI runner GCC version | Copy proposed values from the CI job summary into `coverage-baseline.json` |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 120s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
