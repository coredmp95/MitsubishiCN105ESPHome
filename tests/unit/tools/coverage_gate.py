## coverage_gate.py — coverage policy CLI (ratchet, scope, diff coverage, sticky comment)
## Role: Holds every coverage gate decision (D-10..D-14) as tested pure functions; CI and coverage.sh only call it.
## Deps: Python standard library only (argparse, decimal, fractions, json). No third-party import.
##
## Exit codes: 0 pass, 1 policy failure, 2 input or usage error (fail closed, never a silent pass).
import argparse
import json
import math
import os
import sys
from decimal import ROUND_FLOOR, Decimal
from fractions import Fraction

TOLERANCE = Fraction(1, 10)
METRICS = ("line", "branch")
TOOLCHAIN_KEYS = ("gcc", "gcovr", "esphome", "os")


class GateInputError(Exception):
    """A missing, malformed or incomplete input. Mapped to exit code 2."""


# ═══════════════════════════════════════════════════════════════════════════
# Input helpers
# ═══════════════════════════════════════════════════════════════════════════


def load_json(path, what):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except FileNotFoundError:
        raise GateInputError("%s not found: %s" % (what, path))
    except (OSError, UnicodeDecodeError) as exc:
        raise GateInputError("%s unreadable (%s): %s" % (what, exc, path))
    except json.JSONDecodeError as exc:
        raise GateInputError("%s is not valid JSON (%s): %s" % (what, exc, path))


def _is_count(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def _is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def counts_from_summary(summary, what="summary"):
    """Return {"line": {"covered", "total"}, "branch": {...}} from a gcovr --json-summary object."""
    if not isinstance(summary, dict):
        raise GateInputError("%s must be a JSON object" % what)
    counts = {}
    for metric in METRICS:
        covered = summary.get(metric + "_covered")
        total = summary.get(metric + "_total")
        if not _is_count(covered) or not _is_count(total):
            raise GateInputError(
                "%s lacks integer %s_covered / %s_total" % (what, metric, metric)
            )
        if covered > total:
            raise GateInputError("%s has %s_covered > %s_total" % (what, metric, metric))
        counts[metric] = {"covered": covered, "total": total}
    return counts


def check_baseline(baseline, what="baseline"):
    if not isinstance(baseline, dict):
        raise GateInputError("%s must be a JSON object" % what)
    for metric in METRICS:
        if not _is_number(baseline.get(metric)):
            raise GateInputError("%s lacks a numeric '%s' value" % (what, metric))
    return baseline


# ═══════════════════════════════════════════════════════════════════════════
# Ratchet (D-11, D-12)
# ═══════════════════════════════════════════════════════════════════════════


def percent(covered, total):
    """Exact percentage of covered/total; an empty denominator counts as fully covered."""
    if total == 0:
        return Fraction(100)
    return Fraction(covered * 100, total)


def baseline_percent(value):
    """Exact value of a baseline percentage as written in the JSON file (no binary float error)."""
    return Fraction(Decimal(str(value)))


def floor_2(value):
    """Floor an exact percentage to 2 decimals, returned as a float that prints as that decimal."""
    floored = Decimal(math.floor(value * 100)).scaleb(-2)
    return float(floored)


def evaluate_ratchet(current, baseline, tolerance=TOLERANCE):
    """Compare current counts with the baseline percentages.

    current:  {"line": {"covered": int, "total": int}, "branch": {...}}
    baseline: {"line": number, "branch": number, ...}
    Returns (status, per_metric) where status is FAIL > RATCHET > OK, computed only after both
    metrics are evaluated so the evaluation order cannot change the verdict.
    """
    tolerance = Fraction(tolerance)
    per_metric = {}
    for metric in METRICS:
        covered = current[metric]["covered"]
        total = current[metric]["total"]
        cur = percent(covered, total)
        base = baseline_percent(baseline[metric])
        delta = cur - base
        if delta < -tolerance:
            verdict = "FAIL"
        elif delta > tolerance:
            verdict = "RATCHET"
        else:
            verdict = "OK"
        per_metric[metric] = {
            "status": verdict,
            "current": cur,
            "baseline": base,
            "delta": delta,
            "covered": covered,
            "total": total,
        }
    statuses = [m["status"] for m in per_metric.values()]
    if "FAIL" in statuses:
        overall = "FAIL"
    elif "RATCHET" in statuses:
        overall = "RATCHET"
    else:
        overall = "OK"
    return overall, per_metric


def build_proposal(current, toolchain, source):
    """A complete baseline object for the given counts (percentages floored to 2 decimals)."""
    proposal = {}
    for metric in METRICS:
        proposal[metric] = floor_2(percent(current[metric]["covered"], current[metric]["total"]))
    for metric in METRICS:
        proposal[metric + "_covered"] = current[metric]["covered"]
        proposal[metric + "_total"] = current[metric]["total"]
    proposal["toolchain"] = toolchain
    proposal["source"] = source
    return proposal


def _fmt(value):
    return "%.2f" % float(value)


def _signed(value):
    return "%+.2f" % float(value)


def format_metric_line(metric, res):
    return "%s: %s current %s%% (%d/%d) baseline %s%% delta %s" % (
        metric,
        res["status"],
        _fmt(res["current"]),
        res["covered"],
        res["total"],
        _fmt(res["baseline"]),
        _signed(res["delta"]),
    )


def render_ratchet_markdown(status, per_metric, proposal):
    lines = [
        "| Metric | Status | Current | Baseline | Delta |",
        "|---|---|---|---|---|",
    ]
    for metric in METRICS:
        res = per_metric[metric]
        lines.append(
            "| %s | %s | %s%% (%d/%d) | %s%% | %s |"
            % (
                metric,
                res["status"],
                _fmt(res["current"]),
                res["covered"],
                res["total"],
                _fmt(res["baseline"]),
                _signed(res["delta"]),
            )
        )
    lines.append("")
    lines.append("**Coverage ratchet: %s**" % status)
    if status == "RATCHET":
        lines.append("")
        lines.append("Commit this as tests/unit/coverage-baseline.json")
        lines.append("")
        lines.append("```json")
        lines.append(json.dumps(proposal, indent=2))
        lines.append("```")
    return "\n".join(lines) + "\n"


def write_text(path, text):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


def write_json_file(path, obj):
    write_text(path, json.dumps(obj, indent=2) + "\n")


def cmd_ratchet(args):
    summary = load_json(args.summary, "summary")
    baseline = check_baseline(load_json(args.baseline, "baseline"))
    current = counts_from_summary(summary)

    status, per_metric = evaluate_ratchet(current, baseline, TOLERANCE)

    toolchain = baseline.get("toolchain") if isinstance(baseline.get("toolchain"), dict) else {}
    proposal = build_proposal(current, toolchain, "coverage_gate.py ratchet proposal")

    for metric in METRICS:
        print(format_metric_line(metric, per_metric[metric]))
    print("Coverage ratchet: %s" % status)
    if status == "RATCHET":
        print("Proposed baseline (commit this as tests/unit/coverage-baseline.json):")
        print(json.dumps(proposal, indent=2))

    if args.markdown:
        write_text(args.markdown, render_ratchet_markdown(status, per_metric, proposal))
    if args.json:
        verdict = {
            "status": status,
            "metrics": {
                m: {
                    "status": per_metric[m]["status"],
                    "current": float(per_metric[m]["current"]),
                    "baseline": float(per_metric[m]["baseline"]),
                    "delta": float(per_metric[m]["delta"]),
                    "covered": per_metric[m]["covered"],
                    "total": per_metric[m]["total"],
                }
                for m in METRICS
            },
        }
        if status == "RATCHET":
            verdict["proposal"] = proposal
        write_json_file(args.json, verdict)
    return 1 if status == "FAIL" else 0


# ═══════════════════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════════════════


def build_parser():
    parser = argparse.ArgumentParser(prog="coverage_gate.py", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("ratchet", help="compare a gcovr summary with the committed baseline")
    p.add_argument("--summary", required=True)
    p.add_argument("--baseline", required=True)
    p.add_argument("--markdown")
    p.add_argument("--json")
    p.set_defaults(func=cmd_ratchet)
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except GateInputError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
