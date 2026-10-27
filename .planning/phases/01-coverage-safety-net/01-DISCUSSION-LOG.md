# Phase 1: Coverage Safety Net - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-10-27
**Phase:** 01-coverage-safety-net
**Areas discussed:** Test harness, Tooling & reporting, Baseline rule (ratchet), Characterization test style

---

## Test harness

| Option | Description | Selected |
|--------|-------------|----------|
| Real ESPHome headers | Pinned ESPHome core via FetchContent, host platform, hand-written defines.h | ✓ |
| Hand-written stubs | Extend tests/unit/mocks/ for the whole API surface | |
| Hybrid | Real headers for simple types, stubs for hardware/scheduler | |

| Option | Description | Selected |
|--------|-------------|----------|
| CI `stable` version | Pin the exact version CI uses, bump manually | ✓ |
| Minimum supported (2025.7) | Test oldest compat paths | |
| Both | Two binaries | |

| Option | Description | Selected |
|--------|-------------|----------|
| Neutral seams, no test #ifdef | Behaviour-neutral changes only | ✓ |
| Zero changes | Production untouched | |
| Test #ifdef allowed | Conditional code in component | |

| Option | Description | Selected |
|--------|-------------|----------|
| Both ESP32 and ESP8266 variants | Two test configurations | ✓ |
| ESP32 only | ESP8266 checked by CI compile only | |

**Notes:** ESPHome stable resolved to 2026.10.0 on 2026-10-27.

---

## Tooling & reporting

| Option | Description | Selected |
|--------|-------------|----------|
| In-house | gcovr, job summary, HTML artifact | ✓ |
| Codecov / Coveralls | External service, token | |

| Option | Description | Selected |
|--------|-------------|----------|
| Job summary + PR comment | diff-cover, comment via GITHUB_TOKEN | ✓ |
| Job summary only | | |
| Diff annotations | | |

| Option | Description | Selected |
|--------|-------------|----------|
| Script | tests/unit/coverage.sh, also used by CI | ✓ |
| CMake target | | |
| You decide | | |

| Option | Description | Selected |
|--------|-------------|----------|
| GCC + gcovr | Cobertura XML for diff-cover | ✓ |
| Clang + llvm-cov | | |
| You decide | | |

---

## Baseline rule (ratchet)

| Option | Description | Selected |
|--------|-------------|----------|
| All cn105 .cpp/.h | Uncompiled files count 0% | ✓ |
| Compiled files only | | |

| Option | Description | Selected |
|--------|-------------|----------|
| Lines + branches | Separate baselines | ✓ |
| Lines only | | |

| Option | Description | Selected |
|--------|-------------|----------|
| Committed file, 0.1 pt tolerance | coverage-baseline.json, bumped in PR | ✓ |
| Auto-update on main | | |
| Zero tolerance | | |

| Option | Description | Selected |
|--------|-------------|----------|
| Blocking at 80% | coverage-exempt label for exceptions | ✓ |
| Informative only | | |
| Blocking at 100% | | |

---

## Characterization test style

| Option | Description | Selected |
|--------|-------------|----------|
| UART bytes + published state | | ✓ |
| Published state only | | |
| UART bytes only | | |

| Option | Description | Selected |
|--------|-------------|----------|
| Name + comment linked to bug ID | Characterize_BUGnn_…, flipped by fixing phase | ✓ |
| DISABLED_ tests of correct behaviour | | |
| Both | | |

| Option | Description | Selected |
|--------|-------------|----------|
| Milestone target paths | COV-04 list only | |
| As much as possible | Also sensors, selects, installer functions | ✓ |

| Option | Description | Selected |
|--------|-------------|----------|
| One file per module | tests/unit/characterization/, shared TEST_F fixture | ✓ |
| Flat next to existing tests | | |

Follow-up on depth:

| Option | Description | Selected |
|--------|-------------|----------|
| Prioritized, 2 PRs | 1a target paths (COV-01..04), 1b extension | ✓ |
| Single PR | | |
| Numeric target | | |

---

## Claude's Discretion

- FetchContent mechanics and defines.h content; fake UART and HA-state observation design; gcovr branch-noise options; CI job layout; whether existing pure tests move to the new harness.

## Deferred Ideas

None.

## Other notes from the user during discussion

- "si à un moment tu estimes qu'on a besoin de vraies trames, signale-le moi" — flag explicitly when real captures are needed.
