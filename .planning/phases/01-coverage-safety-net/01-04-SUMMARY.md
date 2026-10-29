---
phase: 01-coverage-safety-net
plan: 04
subsystem: testing
tags: [python, stdlib, unittest, coverage-gate, ratchet, diff-cover, cobertura, gcovr]

requires: []
provides:
  - "tests/unit/tools/coverage_gate.py: stdlib-only CLI with ratchet, propose, scope, complete, diff, comment-target (exit 0 pass / 1 policy failure / 2 input error)"
  - "tests/unit/tools/test_coverage_gate.py: 69-test unittest self-test, the permanent D-14 demonstration (a drop fails, a rise proposes a ratchet)"
  - "JSON verdict objects with a 'status' key for ratchet --json and diff --json, so CI never parses text"
affects: [01-05, 01-06, 01-11, 01-15]

actuals:
  tokens: 16614
  tasks: 3
  commits: 6

tech-stack:
  added: []
  patterns:
    - "Gate decisions are pure functions over exact counts (fractions.Fraction), never rounded floats"
    - "Fail closed: every missing file, malformed JSON/XML or missing key exits 2"
    - "Atomic rewrite of summary.json and Cobertura XML (temp file + os.replace) only after both parsed"

key-files:
  created:
    - tests/unit/tools/coverage_gate.py
    - tests/unit/tools/test_coverage_gate.py
  modified: []

key-decisions:
  - "Percentages are Fraction(covered*100, total); baseline values are Fraction(Decimal(str(v))), so a drop of exactly 0.1 point passes and 39.99 vs 40.09 is not misjudged by binary floats"
  - "Overall verdict precedence FAIL > RATCHET > OK is computed after both metrics, so metric order cannot change it"
  - "Lowered-baseline check compares the current baseline file with --previous-baseline; it FAILs unless the toolchain (gcc, gcovr, esphome, os) changed, then it only WARNs; a provisional or absent previous file skips it"
  - "complete adds a 0/0 entry only for a *.h that is absent from gcovr's output AND listed in the compiled-headers manifest; never a .cpp, never an unlisted header, top-level totals untouched, idempotent"
  - "diff uses the integer comparison covered*100 >= threshold*total, so exactly 80% passes; EXEMPT still reports the measured percentage"

patterns-established:
  - "Each task = RED commit (tests, failing on behavior) then GREEN commit; Task 1 used a stub module so the RED fails on assertions, not on import"
  - "argparse help strings must escape a literal percent sign (80%%)"

requirements-completed: [COV-02, COV-03]

coverage:
  - id: D1
    description: "Ratchet on line and branch coverage with a 0.1-point tolerance; a drop fails, a rise proposes a floored baseline, boundary exact"
    requirement: COV-02
    verification:
      - kind: unit
        ref: "tests/unit/tools/test_coverage_gate.py#RatchetCliTest.test_cli_drop_fails_the_ratchet_D14"
        status: pass
      - kind: unit
        ref: "tests/unit/tools/test_coverage_gate.py#RatchetCliTest.test_cli_rise_proposes_a_ratchet_D14"
        status: pass
      - kind: unit
        ref: "tests/unit/tools/test_coverage_gate.py#EvaluateRatchetTest.test_drop_of_exactly_tolerance_passes"
        status: pass
    human_judgment: false
  - id: D2
    description: "Baseline lifecycle: propose, toolchain-drift warning, 'baseline only goes up' against the base branch"
    requirement: COV-02
    verification:
      - kind: unit
        ref: "tests/unit/tools/test_coverage_gate.py#BaselineLifecycleTest (11 tests)"
        status: pass
    human_judgment: false
  - id: D3
    description: "D-10 scope check over every .cpp and .h, and 'complete' listing declaration-only compiled headers as 0/0 in summary.json and Cobertura XML"
    requirement: COV-02
    verification:
      - kind: unit
        ref: "tests/unit/tools/test_coverage_gate.py#ScopeTest (8 tests), #CompleteTest (8 tests)"
        status: pass
    human_judgment: false
  - id: D4
    description: "80% blocking diff-coverage rule, coverage-exempt exemption, NO_LINES, and deterministic sticky-comment selection"
    requirement: COV-03
    verification:
      - kind: unit
        ref: "tests/unit/tools/test_coverage_gate.py#DiffCoverageTest (13 tests), #CommentTargetTest (9 tests)"
        status: pass
    human_judgment: false

duration: ~30min
completed: 2026-10-29
status: complete
plan_head_before: d255b6c38692d526210b74c4188dc70d910b5f49
plan_head_after: 5cefe69e2820ba953cfe40a56a594e3309c7690d
commits: 6
---

# Phase 1 Plan 04: Coverage gate CLI Summary

**A stdlib-only `coverage_gate.py` holds every coverage-policy decision (exact-count line+branch ratchet with 0.1-point tolerance, baseline-only-goes-up, D-10 .cpp/.h scope with 0/0 header completion, 80% diff rule with `coverage-exempt`, sticky-comment selection), proven by a 69-test unittest self-test that demonstrates both D-14 outcomes permanently.**

## Performance

- **Duration:** ~30 min (start time not captured at plan start; estimated)
- **Completed:** 2026-10-29T20:30Z
- **Tasks:** 3 (each as RED + GREEN commits)
- **Files:** 2 created (807 + 773 lines)

## Accomplishments

- `ratchet`: exact `Fraction` arithmetic, FAIL > RATCHET > OK, proposal floored to 2 decimals with exact counts, markdown with the "Commit this as tests/unit/coverage-baseline.json" line, JSON verdict with `status`.
- `propose`, toolchain-drift WARNING, `--previous-baseline` lowered check (FAIL unless toolchain changed; provisional/absent skipped) - mitigates T-01-04.
- `scope`: hard failure with separate ".cpp missing" / ".h missing" headings; `no_executable_lines` entries count as present.
- `complete`: manifest-listed declaration-only headers get a 0/0 `no_executable_lines` entry in the gcovr summary and an empty `<class>` in the Cobertura XML (DOCTYPE preserved, atomic, idempotent, untouched on error) - mitigates T-01-32.
- `diff`: integer comparison so exactly 80.0% passes, 77.78% fails; EXEMPT keeps the measured percentage; NO_LINES for an empty diff; per-file uncovered-line table.
- `comment-target`: smallest marker id (numeric) from a JSON array or JSON Lines, nothing when none matches.

## Task Commits

1. **Task 1 (tracer): ratchet verdict end to end** - `0164771` (test, RED against a stub) -> `7d96f06` (feat, GREEN)
2. **Task 2: baseline lifecycle, scope, complete** - `e475510` (test, RED) -> `a649151` (feat, GREEN)
3. **Task 3: diff rule and comment-target** - `35aba42` (test, RED) -> `5cefe69` (feat, GREEN)

Tracer gate (auto/end-of-phase, automated-only verify): `python3 -m unittest discover -s tests/unit/tools -v` re-run end to end after Task 1, passed - tracer verified, expanded to Tasks 2-3.

## Files Created/Modified

- `tests/unit/tools/coverage_gate.py` - the CLI (subcommands listed above); the contract consumed by plans 01-05 (`coverage.sh`) and 01-06 (`ci_coverage_gate.sh`).
- `tests/unit/tools/test_coverage_gate.py` - self-test; run with `python3 -m unittest discover -s tests/unit/tools` (not `-I`).

## Decisions Made

See `key-decisions` above. Additional: `ratchet` accepts an optional `--source` (not in the interface block) to label the proposal; without it the proposal's source is "coverage_gate.py ratchet proposal". When `--toolchain` is absent the proposal carries the baseline's toolchain.

## Deviations from Plan

None - plan executed exactly as written. (An argparse help string initially contained an unescaped `80%`, which crashed `--help`; caught by the GREEN run and fixed before the Task 3 GREEN commit - an in-task fix, not a plan deviation.)

**Total deviations:** 0.

## Issues Encountered

- RED for Tasks 2 and 3 includes a few tests that exercise exit code 2 on bad input; they pass against the not-yet-implemented CLI because argparse also exits 2. The behavior-bearing tests in each RED commit failed on assertions (20 of 47 and 14 of 69 failing respectively).
- Plan start time was not recorded, so `duration` is an estimate.

## Verification Results

- `python3 -m unittest discover -s tests/unit/tools -v`: 69 tests, OK (names containing scope, complete, diff and comment all appear).
- Stdlib-only AST check: passes; `Fraction` used throughout; `no_executable_lines`, `total_num_violations` present in the gate; `cn105-coverage-report` present in the self-test.
- `coverage_gate.py scope --summary /nonexistent.json ...` and `complete ... /nonexistent ...` both exit 2.
- `coverage_gate.py --help` lists ratchet, propose, scope, complete, diff, comment-target.
- `git diff d255b6c..HEAD -- components/cn105` is empty (no production source touched).

## User Setup Required

None.

## Known Stubs

None. (The Task 1 RED stub of `coverage_gate.py` was replaced by the real implementation in the same task.)

## Threat Flags

None - stdlib-only local tooling; no new network endpoint, auth path or trust boundary beyond the plan's threat register (T-01-04, T-01-05, T-01-32 mitigated and tested).

## Next Phase Readiness

- Ready for 01-05 (`coverage.sh` writes the toolchain JSON, the compiled-headers manifest and calls `scope`/`complete`/`ratchet`) and 01-06 (CI wiring: `diff`, `comment-target`, `--previous-baseline` against the base branch).
- The first real baseline must still come from an ubuntu-24.04 CI run (D-20); this plan only provides `propose`/`ratchet`.

## Self-Check: PASSED

- FOUND: tests/unit/tools/coverage_gate.py, tests/unit/tools/test_coverage_gate.py
- FOUND commits: 0164771, 7d96f06, e475510, a649151, 35aba42, 5cefe69
