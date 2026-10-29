## test_coverage_gate.py — unittest self-test of every coverage gate rule and edge (D-14)
## Role: Hermetic, permanent demonstration that a coverage drop fails and a rise proposes a ratchet.
## Deps: Python standard library only; exercises coverage_gate.py directly and through its CLI.
import json
import os
import subprocess
import sys
import tempfile
import unittest
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import coverage_gate  # noqa: E402

GATE = os.path.join(HERE, "coverage_gate.py")

TOOLCHAIN = {"gcc": "13.3.0", "gcovr": "8.6", "esphome": "2026.10.0", "os": "ubuntu 24.04"}


def make_summary(line_cov, line_tot, branch_cov, branch_tot, files=None):
    return {
        "line_covered": line_cov,
        "line_total": line_tot,
        "branch_covered": branch_cov,
        "branch_total": branch_tot,
        "files": files if files is not None else [],
    }


def make_baseline(line, branch, **extra):
    base = {"line": line, "branch": branch, "toolchain": dict(TOOLCHAIN), "source": "test"}
    base.update(extra)
    return base


class TempDirTestCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = self._tmp.name

    def path(self, name):
        return os.path.join(self.tmp, name)

    def write_json(self, name, obj):
        p = self.path(name)
        with open(p, "w", encoding="utf-8") as fh:
            json.dump(obj, fh)
        return p

    def write_text(self, name, text):
        p = self.path(name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", encoding="utf-8") as fh:
            fh.write(text)
        return p

    def run_cli(self, *args, cwd=None):
        return subprocess.run(
            [sys.executable, GATE, *args],
            capture_output=True,
            text=True,
            cwd=cwd,
        )


class EvaluateRatchetTest(unittest.TestCase):
    def counts(self, lc, lt, bc, bt):
        return {"line": {"covered": lc, "total": lt}, "branch": {"covered": bc, "total": bt}}

    def evaluate(self, lc, lt, bc, bt, line, branch):
        return coverage_gate.evaluate_ratchet(
            self.counts(lc, lt, bc, bt), {"line": line, "branch": branch}, Fraction(1, 10)
        )

    def test_drop_of_exactly_tolerance_passes(self):
        status, res = self.evaluate(399, 1000, 300, 1000, 40.0, 30.0)
        self.assertEqual(status, "OK")
        self.assertEqual(res["line"]["status"], "OK")

    def test_one_more_uncovered_line_beyond_tolerance_fails(self):
        status, res = self.evaluate(3989, 10000, 300, 1000, 40.0, 30.0)
        self.assertEqual(status, "FAIL")
        self.assertEqual(res["line"]["status"], "FAIL")
        self.assertEqual(res["branch"]["status"], "OK")

    def test_rise_beyond_tolerance_is_ratchet(self):
        status, res = self.evaluate(450, 1000, 350, 1000, 40.0, 30.0)
        self.assertEqual(status, "RATCHET")
        self.assertEqual(res["line"]["status"], "RATCHET")

    def test_equal_values_are_ok(self):
        status, _ = self.evaluate(400, 1000, 300, 1000, 40.0, 30.0)
        self.assertEqual(status, "OK")

    def test_line_falls_while_branch_rises_is_fail(self):
        status, _ = self.evaluate(390, 1000, 350, 1000, 40.0, 30.0)
        self.assertEqual(status, "FAIL")

    def test_branch_falls_while_line_rises_is_fail(self):
        status, _ = self.evaluate(450, 1000, 290, 1000, 40.0, 30.0)
        self.assertEqual(status, "FAIL")

    def test_exact_arithmetic_beats_float_rounding(self):
        # 39.99 - 40.09 is -0.10000000000000142 in binary floats, which would wrongly fail.
        status, _ = self.evaluate(3999, 10000, 300, 1000, 40.09, 30.0)
        self.assertEqual(status, "OK")

    def test_zero_total_counts_as_fully_covered(self):
        status, res = self.evaluate(0, 0, 0, 0, 100.0, 100.0)
        self.assertEqual(status, "OK")
        self.assertEqual(res["line"]["current"], Fraction(100))

    def test_provisional_zero_baseline_ratchets_on_real_data(self):
        status, _ = self.evaluate(10, 100, 5, 50, 0.0, 0.0)
        self.assertEqual(status, "RATCHET")


class RatchetCliTest(TempDirTestCase):
    def ratchet(self, summary, baseline, *extra):
        s = self.write_json("summary.json", summary)
        b = self.write_json("baseline.json", baseline)
        return self.run_cli("ratchet", "--summary", s, "--baseline", b, *extra)

    def test_cli_drop_of_exactly_point_one_passes(self):
        r = self.ratchet(make_summary(399, 1000, 300, 1000), make_baseline(40.0, 30.0))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("Coverage ratchet: OK", r.stdout)

    def test_cli_drop_fails_the_ratchet_D14(self):
        r = self.ratchet(make_summary(3989, 10000, 300, 1000), make_baseline(40.0, 30.0))
        self.assertEqual(r.returncode, 1)
        self.assertIn("Coverage ratchet: FAIL", r.stdout)

    def test_cli_rise_proposes_a_ratchet_D14(self):
        r = self.ratchet(make_summary(450, 1000, 350, 1000), make_baseline(40.0, 30.0))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("Coverage ratchet: RATCHET", r.stdout)
        start = r.stdout.index("{")
        proposal = json.loads(r.stdout[start:r.stdout.rindex("}") + 1])
        self.assertEqual(proposal["line"], 45.0)
        self.assertEqual(proposal["branch"], 35.0)
        self.assertEqual(proposal["line_covered"], 450)
        self.assertEqual(proposal["branch_total"], 1000)

    def test_proposal_floors_to_two_decimals(self):
        r = self.ratchet(make_summary(2, 3, 1, 1), make_baseline(50.0, 50.0))
        self.assertEqual(r.returncode, 0, r.stderr)
        proposal = json.loads(r.stdout[r.stdout.index("{"):r.stdout.rindex("}") + 1])
        self.assertEqual(proposal["line"], 66.66)

    def test_markdown_and_json_verdict_files(self):
        md = self.path("out.md")
        js = self.path("out.json")
        r = self.ratchet(
            make_summary(450, 1000, 350, 1000),
            make_baseline(40.0, 30.0),
            "--markdown", md, "--json", js,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(md, encoding="utf-8") as fh:
            text = fh.read()
        self.assertIn("**Coverage ratchet: RATCHET**", text)
        self.assertIn("Commit this as tests/unit/coverage-baseline.json", text)
        self.assertIn("```json", text)
        with open(js, encoding="utf-8") as fh:
            verdict = json.load(fh)
        self.assertEqual(verdict["status"], "RATCHET")

    def test_provisional_baseline_ratchets(self):
        base = {"line": 0.0, "branch": 0.0, "provisional": True, "toolchain": TOOLCHAIN, "source": "prov"}
        r = self.ratchet(make_summary(10, 100, 5, 50), base)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("Coverage ratchet: RATCHET", r.stdout)

    def test_missing_summary_exits_two(self):
        b = self.write_json("baseline.json", make_baseline(40.0, 30.0))
        r = self.run_cli("ratchet", "--summary", self.path("nope.json"), "--baseline", b)
        self.assertEqual(r.returncode, 2)

    def test_invalid_json_exits_two(self):
        s = self.write_text("summary.json", "{not json")
        b = self.write_json("baseline.json", make_baseline(40.0, 30.0))
        r = self.run_cli("ratchet", "--summary", s, "--baseline", b)
        self.assertEqual(r.returncode, 2)

    def test_summary_without_branch_total_exits_two(self):
        summary = make_summary(1, 2, 1, 2)
        del summary["branch_total"]
        r = self.ratchet(summary, make_baseline(40.0, 30.0))
        self.assertEqual(r.returncode, 2)

    def test_baseline_without_branch_exits_two(self):
        r = self.ratchet(make_summary(1, 2, 1, 2), {"line": 40.0})
        self.assertEqual(r.returncode, 2)


if __name__ == "__main__":
    unittest.main()
