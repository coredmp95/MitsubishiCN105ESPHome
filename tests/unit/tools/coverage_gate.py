## coverage_gate.py — coverage policy CLI (ratchet, scope, diff coverage, sticky comment)
## Role: Holds every coverage gate decision (D-10..D-14) as tested pure functions; CI and coverage.sh only call it.
## Deps: Python standard library only (argparse, decimal, fractions, json, xml.etree). No third-party import.
##
## Exit codes: 0 pass, 1 policy failure, 2 input or usage error (fail closed, never a silent pass).
import argparse
import json
import math
import os
import posixpath
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
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


def render_ratchet_markdown(status, per_metric, proposal, warnings=(), reasons=()):
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
    for message in warnings:
        lines.append(message)
        lines.append("")
    for message in reasons:
        lines.append("FAIL: " + message)
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


def toolchains_differ(first, second):
    """True when two toolchain objects disagree on gcc, gcovr, esphome or os."""
    first = first if isinstance(first, dict) else {}
    second = second if isinstance(second, dict) else {}
    return any(first.get(k) != second.get(k) for k in TOOLCHAIN_KEYS)


def _toolchain_text(toolchain):
    toolchain = toolchain if isinstance(toolchain, dict) else {}
    return ", ".join("%s %s" % (k, toolchain.get(k, "?")) for k in TOOLCHAIN_KEYS)


def check_lowered_baseline(baseline, previous):
    """The 'baseline only goes up' rule against the base branch's baseline (T-01-04).

    Returns (failed, message). A lowered baseline fails unless the recorded toolchain changed,
    in which case it only warns. A provisional previous baseline is never compared.
    """
    if previous.get("provisional"):
        return False, None
    lowered = [
        m
        for m in METRICS
        if baseline_percent(baseline[m]) < baseline_percent(previous[m])
    ]
    if not lowered:
        return False, None
    detail = ", ".join(
        "%s %s%% -> %s%%" % (m, _fmt(baseline_percent(previous[m])), _fmt(baseline_percent(baseline[m])))
        for m in lowered
    )
    if toolchains_differ(baseline.get("toolchain"), previous.get("toolchain")):
        return False, (
            "WARNING: baseline lowered (%s) but the toolchain changed (%s -> %s)"
            % (detail, _toolchain_text(previous.get("toolchain")), _toolchain_text(baseline.get("toolchain")))
        )
    return True, "baseline lowered (%s) with an unchanged toolchain" % detail


def cmd_ratchet(args):
    summary = load_json(args.summary, "summary")
    baseline = check_baseline(load_json(args.baseline, "baseline"))
    current = counts_from_summary(summary)

    status, per_metric = evaluate_ratchet(current, baseline, TOLERANCE)
    warnings = []
    reasons = []

    baseline_toolchain = baseline.get("toolchain") if isinstance(baseline.get("toolchain"), dict) else {}
    if args.toolchain:
        toolchain = load_json(args.toolchain, "toolchain")
        if not isinstance(toolchain, dict):
            raise GateInputError("toolchain must be a JSON object")
        if baseline_toolchain and toolchains_differ(toolchain, baseline_toolchain):
            warnings.append(
                "WARNING: toolchain differs from baseline (baseline: %s; current: %s)"
                % (_toolchain_text(baseline_toolchain), _toolchain_text(toolchain))
            )
    else:
        toolchain = baseline_toolchain

    if args.previous_baseline and os.path.exists(args.previous_baseline):
        previous = check_baseline(
            load_json(args.previous_baseline, "previous baseline"), "previous baseline"
        )
        failed, message = check_lowered_baseline(baseline, previous)
        if failed:
            reasons.append(message)
            status = "FAIL"
        elif message:
            warnings.append(message)

    proposal = build_proposal(current, toolchain, args.source or "coverage_gate.py ratchet proposal")

    for metric in METRICS:
        print(format_metric_line(metric, per_metric[metric]))
    for line in warnings:
        print(line)
    for line in reasons:
        print("FAIL: " + line)
    print("Coverage ratchet: %s" % status)
    if status == "RATCHET":
        print("Proposed baseline (commit this as tests/unit/coverage-baseline.json):")
        print(json.dumps(proposal, indent=2))

    if args.markdown:
        write_text(
            args.markdown,
            render_ratchet_markdown(status, per_metric, proposal, warnings, reasons),
        )
    if args.json:
        verdict = {
            "status": status,
            "warnings": warnings,
            "reasons": reasons,
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


def cmd_propose(args):
    summary = load_json(args.summary, "summary")
    toolchain = load_json(args.toolchain, "toolchain")
    if not isinstance(toolchain, dict):
        raise GateInputError("toolchain must be a JSON object")
    current = counts_from_summary(summary)
    print(json.dumps(build_proposal(current, toolchain, args.source), indent=2))
    return 0


# ═══════════════════════════════════════════════════════════════════════════
# D-10 scope: every .cpp and .h under the source dir is reported
# ═══════════════════════════════════════════════════════════════════════════


def _norm(path):
    return posixpath.normpath(path.replace(os.sep, "/"))


def summary_files(summary, what="summary"):
    """The files[] list of a gcovr --json-summary object, validated."""
    files = summary.get("files") if isinstance(summary, dict) else None
    if not isinstance(files, list):
        raise GateInputError("%s has no files[] list" % what)
    for item in files:
        if not isinstance(item, dict) or not isinstance(item.get("filename"), str):
            raise GateInputError("%s has a files[] entry without a filename" % what)
    return files


def source_files(source_dir):
    """Root-relative paths of the *.cpp and *.h files directly under source_dir, sorted."""
    if not os.path.isdir(source_dir):
        raise GateInputError("source directory not found: %s" % source_dir)
    base = _norm(source_dir)
    names = sorted(
        n
        for n in os.listdir(source_dir)
        if n.endswith((".cpp", ".h")) and os.path.isfile(os.path.join(source_dir, n))
    )
    return [posixpath.join(base, n) for n in names]


def missing_from_summary(summary, source_dir):
    reported = {_norm(f["filename"]) for f in summary_files(summary)}
    missing = [p for p in source_files(source_dir) if p not in reported]
    return (
        [p for p in missing if p.endswith(".cpp")],
        [p for p in missing if p.endswith(".h")],
    )


def render_scope(missing_cpp, missing_h):
    lines = []
    if missing_cpp or missing_h:
        lines.append("**Coverage scope: FAIL** - files under the source directory are absent from the report")
    else:
        lines.append("**Coverage scope: OK**")
    for heading, items in ((".cpp missing", missing_cpp), (".h missing", missing_h)):
        if items:
            lines.append("")
            lines.append("%s:" % heading)
            lines.extend("- %s" % item for item in items)
    return "\n".join(lines) + "\n"


def cmd_scope(args):
    summary = load_json(args.summary, "summary")
    missing_cpp, missing_h = missing_from_summary(summary, args.source_dir)
    text = render_scope(missing_cpp, missing_h)
    print(text, end="")
    if args.markdown:
        write_text(args.markdown, text)
    return 1 if (missing_cpp or missing_h) else 0


# ═══════════════════════════════════════════════════════════════════════════
# D-10 complete: declaration-only headers are listed as 0/0
# ═══════════════════════════════════════════════════════════════════════════

NO_LINES_HEADING = "Headers with no executable lines (0/0)"
NO_LINES_SOURCES_HEADING = "Sources with no executable lines (0/0)"
XML_DECL = re.compile(r"^\s*(<\?xml[^>]*\?>)")
XML_DOCTYPE = re.compile(r"<!DOCTYPE[^>]*>")


def read_manifest(path):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            lines = fh.read().splitlines()
    except FileNotFoundError:
        raise GateInputError("compiled-headers manifest not found: %s" % path)
    except (OSError, UnicodeDecodeError) as exc:
        raise GateInputError("compiled-headers manifest unreadable (%s): %s" % (exc, path))
    return {_norm(l.strip()) for l in lines if l.strip() and not l.strip().startswith("#")}


def new_summary_entry(filename, template):
    """A 0/0 entry with the same key set as a gcovr entry, flagged no_executable_lines."""
    entry = {"filename": filename}
    for key, value in (template or {}).items():
        if key == "filename":
            continue
        if key.endswith("_percent"):
            entry[key] = 0.0
        elif _is_number(value):
            entry[key] = 0
        else:
            entry[key] = type(value)() if value is not None else None
    for key in ("line_total", "line_covered", "branch_total", "branch_covered"):
        entry.setdefault(key, 0)
    for key in ("line_percent", "branch_percent"):
        entry.setdefault(key, 0.0)
    entry["no_executable_lines"] = True
    return entry


def _class_dir(cls):
    return posixpath.dirname(_norm(cls.get("filename", "")))


def add_class_to_cobertura(root, filename, source_dir):
    packages = root.find("packages")
    if packages is None:
        raise GateInputError("Cobertura XML has no <packages> element")
    target = None
    for package in packages.findall("package"):
        classes = package.find("classes")
        if classes is not None and any(_class_dir(c) == source_dir for c in classes.findall("class")):
            target = package
            break
    if target is None:
        target = ET.SubElement(
            packages,
            "package",
            {
                "name": source_dir.replace("/", "."),
                "line-rate": "0.0",
                "branch-rate": "0.0",
                "complexity": "0.0",
            },
        )
        ET.SubElement(target, "classes")
    classes = target.find("classes")
    if classes is None:
        classes = ET.SubElement(target, "classes")
    cls = ET.Element(
        "class",
        {
            "name": re.sub(r"[^0-9A-Za-z]", "_", posixpath.basename(filename)),
            "filename": filename,
            "line-rate": "0.0",
            "branch-rate": "0.0",
            "complexity": "0.0",
        },
    )
    ET.SubElement(cls, "methods")
    ET.SubElement(cls, "lines")
    existing = list(classes)
    if existing:
        # Keep the original indentation: the new class takes the old last tail.
        cls.tail = existing[-1].tail
        existing[-1].tail = classes.text if classes.text else existing[-1].tail
    classes.append(cls)


def parse_cobertura(path):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            text = fh.read()
        root = ET.fromstring(text.encode("utf-8"))
    except FileNotFoundError:
        raise GateInputError("Cobertura XML not found: %s" % path)
    except (OSError, UnicodeDecodeError) as exc:
        raise GateInputError("Cobertura XML unreadable (%s): %s" % (exc, path))
    except ET.ParseError as exc:
        raise GateInputError("Cobertura XML is not well-formed (%s): %s" % (exc, path))
    decl = XML_DECL.match(text)
    doctype = XML_DOCTYPE.search(text)
    return root, (decl.group(1) if decl else None), (doctype.group(0) if doctype else None)


def render_cobertura(root, decl, doctype):
    parts = []
    if decl:
        parts.append(decl)
    if doctype:
        parts.append(doctype)
    parts.append(ET.tostring(root, encoding="unicode"))
    return "\n".join(parts) + "\n"


def atomic_write(path, text):
    directory = os.path.dirname(os.path.abspath(path))
    fd, tmp_path = tempfile.mkstemp(dir=directory, prefix=".coverage_gate_")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(text)
        os.replace(tmp_path, path)
    except BaseException:
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)
        raise


def cmd_complete(args):
    summary = load_json(args.summary, "summary")
    files = summary_files(summary)
    root, decl, doctype = parse_cobertura(args.cobertura)
    manifest = read_manifest(args.compiled_headers)

    source_dir = _norm(args.source_dir)
    reported = {_norm(f["filename"]) for f in files}
    added = [
        path
        for path in source_files(args.source_dir)
        if path.endswith((".h", ".cpp")) and path not in reported and path in manifest
    ]

    if added:
        template = files[0] if files else None
        for path in added:
            files.append(new_summary_entry(path, template))
            add_class_to_cobertura(root, path, source_dir)
        files.sort(key=lambda f: f["filename"])
        summary_text = json.dumps(summary, indent=2) + "\n"
        xml_text = render_cobertura(root, decl, doctype)
        atomic_write(args.summary, summary_text)
        atomic_write(args.cobertura, xml_text)

    # A .cpp can also be empty of executable lines (an include and a comment only, e.g. info_request.cpp):
    # gcov emits no record for it, so it is listed 0/0 like a declaration-only header, under its own heading.
    added_headers = [p for p in added if p.endswith(".h")]
    added_sources = [p for p in added if p.endswith(".cpp")]
    lines = []
    if added_headers:
        lines.append(NO_LINES_HEADING + ":")
        lines.extend("- %s" % path for path in added_headers)
    if added_sources:
        if lines:
            lines.append("")
        lines.append(NO_LINES_SOURCES_HEADING + ":")
        lines.extend("- %s" % path for path in added_sources)
    if not added:
        lines = ["No header needed a 0/0 entry."]
    text = "\n".join(lines) + "\n"
    print(text, end="")
    if args.markdown:
        write_text(args.markdown, ("### %s\n\n" % NO_LINES_HEADING if added_headers else "") + text)
    return 0


# ═══════════════════════════════════════════════════════════════════════════
# Diff coverage (D-13)
# ═══════════════════════════════════════════════════════════════════════════


def parse_threshold(text):
    try:
        value = Fraction(Decimal(str(text)))
    except Exception:
        raise GateInputError("threshold is not a number: %s" % text)
    if value < 0 or value > 100:
        raise GateInputError("threshold must be between 0 and 100: %s" % text)
    return value


def evaluate_diff(total, violations, threshold, exempt):
    """Return (status, percent) for a diff-cover result.

    Integer comparison covered * 100 >= threshold * total, so exactly 80% passes.
    percent is a Fraction, or None when no coverable line changed.
    """
    if total == 0:
        return "NO_LINES", None
    covered = total - violations
    percent_value = Fraction(covered * 100, total)
    if covered * 100 >= threshold * total:
        return "PASS", percent_value
    return ("EXEMPT" if exempt else "FAIL"), percent_value


def read_diff_report(path):
    report = load_json(path, "diff-cover report")
    if not isinstance(report, dict):
        raise GateInputError("diff-cover report must be a JSON object")
    total = report.get("total_num_lines")
    violations = report.get("total_num_violations")
    if not _is_count(total) or not _is_count(violations):
        raise GateInputError(
            "diff-cover report lacks integer total_num_lines / total_num_violations"
        )
    if violations > total:
        raise GateInputError("diff-cover report has more violations than changed lines")
    return report, total, violations


def _threshold_number(threshold):
    return int(threshold) if threshold.denominator == 1 else float(threshold)


def render_diff_markdown(status, percent_value, covered, total, threshold, src_stats):
    thr = "%s%%" % _fmt(threshold)
    lines = ["### Diff coverage (changed lines of components/cn105)", ""]
    if status == "NO_LINES":
        lines.append("**Diff coverage: NO_LINES** - no coverable line of components/cn105 changed.")
    else:
        measured = "%s%% (%d/%d changed lines covered)" % (_fmt(percent_value), covered, total)
        if status == "PASS":
            lines.append("**Diff coverage: PASS** - %s, threshold %s." % (measured, thr))
        elif status == "EXEMPT":
            lines.append(
                "**Diff coverage: EXEMPT** - measured %s, below the %s threshold; "
                "exempted by the coverage-exempt label." % (measured, thr)
            )
        else:
            lines.append("**Diff coverage: FAIL** - %s, below the %s threshold." % (measured, thr))
    rows = []
    if isinstance(src_stats, dict):
        for name in sorted(src_stats):
            stats = src_stats[name] if isinstance(src_stats[name], dict) else {}
            pct = stats.get("percent_covered")
            violation_lines = stats.get("violation_lines") or []
            rows.append(
                "| %s | %s | %s |"
                % (
                    name,
                    ("%s%%" % _fmt(pct)) if _is_number(pct) else "n/a",
                    ", ".join(str(n) for n in violation_lines) if violation_lines else "-",
                )
            )
    if rows:
        lines += ["", "| File | Covered | Uncovered changed lines |", "|---|---|---|"] + rows
    return "\n".join(lines) + "\n"


def cmd_diff(args):
    threshold = parse_threshold(args.threshold)
    report, total, violations = read_diff_report(args.report)
    status, percent_value = evaluate_diff(total, violations, threshold, args.exempt)
    covered = total - violations

    if status == "NO_LINES":
        print("Diff coverage: NO_LINES - no coverable line of components/cn105 changed")
    else:
        print(
            "Diff coverage: %s - %s%% (%d/%d changed lines covered), threshold %s%%"
            % (status, _fmt(percent_value), covered, total, _fmt(threshold))
        )
    if args.markdown:
        write_text(
            args.markdown,
            render_diff_markdown(status, percent_value, covered, total, threshold, report.get("src_stats")),
        )
    if args.json:
        write_json_file(
            args.json,
            {
                "status": status,
                "percent": None if percent_value is None else round(float(percent_value), 2),
                "threshold": _threshold_number(threshold),
                "covered": covered,
                "total": total,
                "exempt": bool(args.exempt),
            },
        )
    return 1 if status == "FAIL" else 0


# ═══════════════════════════════════════════════════════════════════════════
# Sticky PR comment (D-07)
# ═══════════════════════════════════════════════════════════════════════════


def parse_comments(path):
    """A JSON array of comment objects, or JSON Lines with one object per line."""
    try:
        with open(path, "r", encoding="utf-8") as fh:
            text = fh.read()
    except FileNotFoundError:
        raise GateInputError("comments file not found: %s" % path)
    except (OSError, UnicodeDecodeError) as exc:
        raise GateInputError("comments file unreadable (%s): %s" % (exc, path))
    if not text.strip():
        return []
    try:
        data = json.loads(text)
        items = data if isinstance(data, list) else [data]
    except json.JSONDecodeError:
        items = []
        for number, line in enumerate(text.splitlines(), 1):
            if not line.strip():
                continue
            try:
                items.append(json.loads(line))
            except json.JSONDecodeError as exc:
                raise GateInputError("comments line %d is not valid JSON (%s)" % (number, exc))
    return items


def select_comment(comments, marker):
    """The smallest id (the oldest comment) whose body contains the marker, or None."""
    ids = []
    for item in comments:
        if not isinstance(item, dict) or not isinstance(item.get("body"), str):
            raise GateInputError("a comment lacks a string body")
        if marker not in item["body"]:
            continue
        try:
            ids.append(int(item["id"]))
        except (KeyError, TypeError, ValueError):
            raise GateInputError("a matching comment lacks an integer id")
    return min(ids) if ids else None


def cmd_comment_target(args):
    target = select_comment(parse_comments(args.comments), args.marker)
    if target is not None:
        print(target)
    return 0


# ═══════════════════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════════════════


def build_parser():
    parser = argparse.ArgumentParser(prog="coverage_gate.py", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("ratchet", help="compare a gcovr summary with the committed baseline")
    p.add_argument("--summary", required=True)
    p.add_argument("--baseline", required=True)
    p.add_argument("--toolchain")
    p.add_argument("--previous-baseline")
    p.add_argument("--source")
    p.add_argument("--markdown")
    p.add_argument("--json")
    p.set_defaults(func=cmd_ratchet)

    p = sub.add_parser("propose", help="print a complete baseline JSON for a gcovr summary")
    p.add_argument("--summary", required=True)
    p.add_argument("--toolchain", required=True)
    p.add_argument("--source", required=True)
    p.set_defaults(func=cmd_propose)

    p = sub.add_parser("scope", help="fail when a .cpp or .h under the source dir is not reported")
    p.add_argument("--summary", required=True)
    p.add_argument("--source-dir", required=True)
    p.add_argument("--markdown")
    p.set_defaults(func=cmd_scope)

    p = sub.add_parser("complete", help="list declaration-only compiled headers as 0/0")
    p.add_argument("--summary", required=True)
    p.add_argument("--cobertura", required=True)
    p.add_argument("--source-dir", required=True)
    p.add_argument("--compiled-headers", required=True)
    p.add_argument("--markdown")
    p.set_defaults(func=cmd_complete)

    p = sub.add_parser("diff", help="apply the 80%% changed-line rule to a diff-cover JSON report")
    p.add_argument("--report", required=True)
    p.add_argument("--threshold", required=True)
    p.add_argument("--exempt", action="store_true")
    p.add_argument("--markdown")
    p.add_argument("--json")
    p.set_defaults(func=cmd_diff)

    p = sub.add_parser("comment-target", help="print the id of the oldest comment holding the marker")
    p.add_argument("--comments", required=True)
    p.add_argument("--marker", required=True)
    p.set_defaults(func=cmd_comment_target)
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
