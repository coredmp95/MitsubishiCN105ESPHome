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


# ═══════════════════════════════════════════════════════════════════════════
# Baseline lifecycle: propose, toolchain drift, "baseline only goes up"
# ═══════════════════════════════════════════════════════════════════════════


class BaselineLifecycleTest(TempDirTestCase):
    def test_propose_round_trip_then_ratchet_is_ok(self):
        s = self.write_json("summary.json", make_summary(1130, 2964, 512, 2015))
        t = self.write_json("toolchain.json", TOOLCHAIN)
        r = self.run_cli("propose", "--summary", s, "--toolchain", t, "--source", "CI run 1")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(r.stdout.endswith("\n"))
        proposal = json.loads(r.stdout)
        self.assertEqual(proposal["line"], 38.12)
        self.assertEqual(proposal["branch"], 25.4)
        self.assertEqual(proposal["line_covered"], 1130)
        self.assertEqual(proposal["line_total"], 2964)
        self.assertEqual(proposal["branch_covered"], 512)
        self.assertEqual(proposal["branch_total"], 2015)
        self.assertEqual(proposal["toolchain"], TOOLCHAIN)
        self.assertEqual(proposal["source"], "CI run 1")
        b = self.write_json("baseline.json", proposal)
        r2 = self.run_cli("ratchet", "--summary", s, "--baseline", b)
        self.assertEqual(r2.returncode, 0, r2.stderr)
        self.assertIn("Coverage ratchet: OK", r2.stdout)

    def test_propose_never_writes_a_file(self):
        s = self.write_json("summary.json", make_summary(1, 2, 1, 2))
        t = self.write_json("toolchain.json", TOOLCHAIN)
        before = set(os.listdir(self.tmp))
        r = self.run_cli("propose", "--summary", s, "--toolchain", t, "--source", "x", cwd=self.tmp)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(set(os.listdir(self.tmp)), before)

    def test_toolchain_drift_warns_but_still_evaluates(self):
        s = self.write_json("summary.json", make_summary(399, 1000, 300, 1000))
        b = self.write_json("baseline.json", make_baseline(40.0, 30.0))
        newer = dict(TOOLCHAIN, gcc="15.2.0")
        t = self.write_json("toolchain.json", newer)
        md = self.path("out.md")
        r = self.run_cli("ratchet", "--summary", s, "--baseline", b, "--toolchain", t, "--markdown", md)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("WARNING: toolchain differs from baseline", r.stdout)
        self.assertIn("15.2.0", r.stdout)
        self.assertIn("Coverage ratchet: OK", r.stdout)
        with open(md, encoding="utf-8") as fh:
            self.assertIn("WARNING: toolchain differs from baseline", fh.read())

    def test_same_toolchain_gives_no_warning(self):
        s = self.write_json("summary.json", make_summary(400, 1000, 300, 1000))
        b = self.write_json("baseline.json", make_baseline(40.0, 30.0))
        t = self.write_json("toolchain.json", TOOLCHAIN)
        r = self.run_cli("ratchet", "--summary", s, "--baseline", b, "--toolchain", t)
        self.assertNotIn("WARNING", r.stdout)

    def _ratchet_with_previous(self, base, previous, toolchain=None):
        s = self.write_json("summary.json", make_summary(390, 1000, 290, 1000))
        b = self.write_json("baseline.json", base)
        args = ["ratchet", "--summary", s, "--baseline", b]
        if previous is not None:
            args += ["--previous-baseline", self.write_json("previous.json", previous)]
        else:
            args += ["--previous-baseline", self.path("absent.json")]
        return self.run_cli(*args)

    def test_lowered_baseline_with_same_toolchain_fails(self):
        # The summary (39.0 / 29.0) matches the lowered baseline, so only the lowering can fail.
        s = self.write_json("summary.json", make_summary(390, 1000, 290, 1000))
        b = self.write_json("baseline.json", make_baseline(39.0, 29.0))
        p = self.write_json("previous.json", make_baseline(40.0, 30.0))
        r = self.run_cli("ratchet", "--summary", s, "--baseline", b, "--previous-baseline", p)
        self.assertEqual(r.returncode, 1)
        self.assertIn("baseline lowered", r.stdout)
        self.assertIn("Coverage ratchet: FAIL", r.stdout)

    def test_lowered_branch_alone_also_fails(self):
        s = self.write_json("summary.json", make_summary(400, 1000, 290, 1000))
        b = self.write_json("baseline.json", make_baseline(40.0, 29.0))
        p = self.write_json("previous.json", make_baseline(40.0, 30.0))
        r = self.run_cli("ratchet", "--summary", s, "--baseline", b, "--previous-baseline", p)
        self.assertEqual(r.returncode, 1)
        self.assertIn("baseline lowered", r.stdout)

    def test_lowered_baseline_with_toolchain_change_only_warns(self):
        s = self.write_json("summary.json", make_summary(390, 1000, 290, 1000))
        lowered = make_baseline(39.0, 29.0)
        lowered["toolchain"] = dict(TOOLCHAIN, gcc="15.2.0")
        b = self.write_json("baseline.json", lowered)
        p = self.write_json("previous.json", make_baseline(40.0, 30.0))
        r = self.run_cli("ratchet", "--summary", s, "--baseline", b, "--previous-baseline", p)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("WARNING", r.stdout)
        self.assertIn("lowered", r.stdout)

    def test_unchanged_or_higher_baseline_passes_the_previous_check(self):
        s = self.write_json("summary.json", make_summary(450, 1000, 350, 1000))
        b = self.write_json("baseline.json", make_baseline(45.0, 35.0))
        p = self.write_json("previous.json", make_baseline(40.0, 30.0))
        r = self.run_cli("ratchet", "--summary", s, "--baseline", b, "--previous-baseline", p)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertNotIn("baseline lowered", r.stdout)

    def test_provisional_previous_baseline_skips_the_check(self):
        prov = {"line": 0.0, "branch": 0.0, "provisional": True, "toolchain": TOOLCHAIN, "source": "p"}
        s = self.write_json("summary.json", make_summary(390, 1000, 290, 1000))
        b = self.write_json("baseline.json", make_baseline(39.0, 29.0))
        p = self.write_json("previous.json", dict(prov, line=50.0, branch=50.0))
        r = self.run_cli("ratchet", "--summary", s, "--baseline", b, "--previous-baseline", p)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_absent_previous_baseline_skips_the_check(self):
        r = self._ratchet_with_previous(make_baseline(39.0, 29.0), None)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertNotIn("baseline lowered", r.stdout)

    def test_malformed_previous_baseline_exits_two(self):
        s = self.write_json("summary.json", make_summary(390, 1000, 290, 1000))
        b = self.write_json("baseline.json", make_baseline(39.0, 29.0))
        p = self.write_text("previous.json", "{broken")
        r = self.run_cli("ratchet", "--summary", s, "--baseline", b, "--previous-baseline", p)
        self.assertEqual(r.returncode, 2)


# ═══════════════════════════════════════════════════════════════════════════
# D-10 scope: every .cpp and .h under components/cn105 must be reported
# ═══════════════════════════════════════════════════════════════════════════

COBERTURA = """<?xml version='1.0' encoding='UTF-8'?>
<!DOCTYPE coverage SYSTEM 'http://cobertura.sourceforge.net/xml/coverage-04.dtd'>
<coverage line-rate="0.5" branch-rate="0.25" lines-covered="1" lines-valid="2" branches-covered="1" branches-valid="4" complexity="0.0" timestamp="1" version="gcovr 8.6">
  <sources>
    <source>.</source>
  </sources>
  <packages>
    <package name="components.cn105" line-rate="0.5" branch-rate="0.25" complexity="0.0">
      <classes>
        <class name="a_h" filename="components/cn105/a.h" line-rate="0.5" branch-rate="0.25" complexity="0.0">
          <methods/>
          <lines>
            <line number="1" hits="1" branch="false"/>
            <line number="2" hits="0" branch="false"/>
          </lines>
        </class>
        <class name="c_cpp" filename="components/cn105/c.cpp" line-rate="0.5" branch-rate="0.25" complexity="0.0">
          <methods/>
          <lines>
            <line number="1" hits="1" branch="false"/>
          </lines>
        </class>
      </classes>
    </package>
  </packages>
</coverage>
"""


def entry(name, lc=1, lt=2, bc=0, bt=0, **extra):
    e = {
        "filename": "components/cn105/" + name,
        "line_covered": lc,
        "line_total": lt,
        "line_percent": 50.0,
        "branch_covered": bc,
        "branch_total": bt,
        "branch_percent": 100.0,
        "function_covered": 0,
        "function_total": 0,
        "function_percent": 100.0,
    }
    e.update(extra)
    return e


class ScopeCompleteTestCase(TempDirTestCase):
    def make_tree(self, names):
        for name in names:
            self.write_text(os.path.join("components", "cn105", name), "// stub\n")

    def scope(self, summary_files, *extra):
        s = self.write_json("summary.json", make_summary(2, 4, 1, 4, summary_files))
        return self.run_cli(
            "scope", "--summary", s, "--source-dir", "components/cn105", *extra, cwd=self.tmp
        )

    def read(self, name):
        with open(self.path(name), "rb") as fh:
            return fh.read()


class ScopeTest(ScopeCompleteTestCase):
    def test_scope_missing_cpp_exits_one_and_lists_it(self):
        self.make_tree(["utils.cpp", "a.cpp"])
        r = self.scope([entry("a.cpp")])
        self.assertEqual(r.returncode, 1)
        self.assertIn("components/cn105/utils.cpp", r.stdout)
        self.assertIn(".cpp missing", r.stdout)

    def test_scope_missing_header_exits_one_and_lists_it(self):
        self.make_tree(["frame_parser.h", "a.cpp"])
        r = self.scope([entry("a.cpp")])
        self.assertEqual(r.returncode, 1)
        self.assertIn("components/cn105/frame_parser.h", r.stdout)
        self.assertIn(".h missing", r.stdout)

    def test_scope_all_present_including_no_executable_lines_header(self):
        self.make_tree(["a.cpp", "types.h", "b.h"])
        files = [
            entry("a.cpp"),
            entry("b.h"),
            entry("types.h", lc=0, lt=0, no_executable_lines=True),
        ]
        r = self.scope(files)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_scope_ignores_other_extensions_and_subdirectories(self):
        self.make_tree(["a.cpp", "README.md", "sub/deep.cpp"])
        r = self.scope([entry("a.cpp")])
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_scope_writes_markdown(self):
        self.make_tree(["a.cpp", "x.h"])
        md = self.path("scope.md")
        r = self.scope([entry("a.cpp")], "--markdown", md)
        self.assertEqual(r.returncode, 1)
        text = self.read("scope.md").decode()
        self.assertIn("components/cn105/x.h", text)
        self.assertIn(".h missing", text)

    def test_scope_missing_summary_exits_two(self):
        r = self.run_cli(
            "scope", "--summary", "/nonexistent.json", "--source-dir", "components/cn105"
        )
        self.assertEqual(r.returncode, 2)

    def test_scope_summary_without_files_exits_two(self):
        self.make_tree(["a.cpp"])
        s = self.write_json("summary.json", {"line_covered": 1})
        r = self.run_cli("scope", "--summary", s, "--source-dir", "components/cn105", cwd=self.tmp)
        self.assertEqual(r.returncode, 2)

    def test_scope_missing_source_dir_exits_two(self):
        s = self.write_json("summary.json", make_summary(0, 0, 0, 0, []))
        r = self.run_cli("scope", "--summary", s, "--source-dir", "components/cn105", cwd=self.tmp)
        self.assertEqual(r.returncode, 2)


class CompleteTest(ScopeCompleteTestCase):
    def setup_case(self, manifest_lines):
        self.make_tree(["a.h", "b.h", "c.cpp", "d.cpp"])
        summary = make_summary(2, 4, 1, 4, [entry("c.cpp"), entry("a.h")])
        self.write_json("summary.json", summary)
        self.write_text("cobertura.xml", COBERTURA)
        self.write_text("manifest.txt", "\n".join(manifest_lines) + "\n")

    def complete(self):
        return self.run_cli(
            "complete",
            "--summary", self.path("summary.json"),
            "--cobertura", self.path("cobertura.xml"),
            "--source-dir", "components/cn105",
            "--compiled-headers", self.path("manifest.txt"),
            cwd=self.tmp,
        )

    def test_complete_adds_manifest_listed_header_to_summary_and_xml(self):
        self.setup_case(["# compiled header TUs", "", "components/cn105/a.h", "components/cn105/b.h"])
        r = self.complete()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("Headers with no executable lines (0/0)", r.stdout)
        self.assertIn("components/cn105/b.h", r.stdout)
        with open(self.path("summary.json"), encoding="utf-8") as fh:
            summary = json.load(fh)
        by_name = {f["filename"]: f for f in summary["files"]}
        added = by_name["components/cn105/b.h"]
        for key in ("line_total", "line_covered", "branch_total", "branch_covered"):
            self.assertEqual(added[key], 0, key)
        self.assertEqual(added["line_percent"], 0.0)
        self.assertEqual(added["branch_percent"], 0.0)
        self.assertIs(added["no_executable_lines"], True)
        self.assertEqual(set(entry("x.h").keys()) | {"no_executable_lines"}, set(added.keys()))
        self.assertEqual([f["filename"] for f in summary["files"]], sorted(by_name))
        self.assertEqual(summary["line_total"], 4)
        self.assertEqual(summary["line_covered"], 2)
        self.assertEqual(summary["branch_total"], 4)
        self.assertEqual(summary["branch_covered"], 1)
        # gcovr's own entries are untouched.
        self.assertEqual(by_name["components/cn105/a.h"], entry("a.h"))
        # Cobertura gets an empty class and keeps the root attributes.
        xml = self.read("cobertura.xml").decode()
        self.assertTrue(xml.startswith("<?xml"))
        self.assertIn("<!DOCTYPE coverage", xml)
        import xml.etree.ElementTree as ET

        root = ET.fromstring(xml.split("?>", 1)[1].split("\n", 2)[2])
        self.assertEqual(root.get("line-rate"), "0.5")
        self.assertEqual(root.get("lines-valid"), "2")
        classes = {c.get("filename"): c for c in root.iter("class")}
        new_class = classes["components/cn105/b.h"]
        self.assertEqual(new_class.get("name"), "b_h")
        self.assertEqual(new_class.get("line-rate"), "0.0")
        self.assertEqual(len(list(new_class.find("lines"))), 0)
        self.assertIsNotNone(new_class.find("methods"))
        self.assertEqual(len(root.findall("./packages/package")), 1)

    def test_complete_never_adds_cpp_or_unlisted_header(self):
        self.setup_case(["components/cn105/a.h"])  # b.h not listed, d.cpp is a source
        before_summary = self.read("summary.json")
        before_xml = self.read("cobertura.xml")
        r = self.complete()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.read("summary.json"), before_summary)
        self.assertEqual(self.read("cobertura.xml"), before_xml)
        scope = self.run_cli(
            "scope", "--summary", self.path("summary.json"), "--source-dir", "components/cn105", cwd=self.tmp
        )
        self.assertEqual(scope.returncode, 1)
        self.assertIn("components/cn105/d.cpp", scope.stdout)
        self.assertIn("components/cn105/b.h", scope.stdout)

    def test_complete_then_scope_names_only_the_missing_cpp(self):
        self.setup_case(["components/cn105/b.h"])
        self.assertEqual(self.complete().returncode, 0)
        scope = self.run_cli(
            "scope", "--summary", self.path("summary.json"), "--source-dir", "components/cn105", cwd=self.tmp
        )
        self.assertEqual(scope.returncode, 1)
        self.assertIn("components/cn105/d.cpp", scope.stdout)
        self.assertNotIn("components/cn105/b.h", scope.stdout)

    def test_complete_is_idempotent(self):
        self.setup_case(["components/cn105/b.h"])
        self.assertEqual(self.complete().returncode, 0)
        first_summary = self.read("summary.json")
        first_xml = self.read("cobertura.xml")
        r = self.complete()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.read("summary.json"), first_summary)
        self.assertEqual(self.read("cobertura.xml"), first_xml)

    def test_complete_creates_a_package_when_none_holds_the_directory(self):
        self.setup_case(["components/cn105/b.h"])
        xml = COBERTURA.replace('filename="components/cn105/', 'filename="other/dir/')
        self.write_text("cobertura.xml", xml)
        self.assertEqual(self.complete().returncode, 0)
        import xml.etree.ElementTree as ET

        xml = self.read("cobertura.xml").decode()
        root = ET.fromstring(xml.split("?>", 1)[1].split("\n", 2)[2])
        packages = root.findall("./packages/package")
        self.assertEqual(len(packages), 2)
        created = packages[1]
        self.assertEqual(created.get("name"), "components.cn105")
        self.assertEqual([c.get("filename") for c in created.iter("class")], ["components/cn105/b.h"])

    def test_complete_with_malformed_xml_exits_two_and_leaves_files_untouched(self):
        self.setup_case(["components/cn105/b.h"])
        self.write_text("cobertura.xml", "<coverage><packages>")
        before_summary = self.read("summary.json")
        before_xml = self.read("cobertura.xml")
        r = self.complete()
        self.assertEqual(r.returncode, 2)
        self.assertEqual(self.read("summary.json"), before_summary)
        self.assertEqual(self.read("cobertura.xml"), before_xml)

    def test_complete_with_missing_manifest_exits_two(self):
        self.setup_case([])
        os.remove(self.path("manifest.txt"))
        before_summary = self.read("summary.json")
        r = self.complete()
        self.assertEqual(r.returncode, 2)
        self.assertEqual(self.read("summary.json"), before_summary)

    def test_complete_with_summary_without_files_exits_two(self):
        self.setup_case(["components/cn105/b.h"])
        self.write_json("summary.json", {"line_covered": 1})
        before_xml = self.read("cobertura.xml")
        r = self.complete()
        self.assertEqual(r.returncode, 2)
        self.assertEqual(self.read("cobertura.xml"), before_xml)

    def test_complete_writes_markdown(self):
        self.setup_case(["components/cn105/b.h"])
        md = self.path("complete.md")
        r = self.run_cli(
            "complete",
            "--summary", self.path("summary.json"),
            "--cobertura", self.path("cobertura.xml"),
            "--source-dir", "components/cn105",
            "--compiled-headers", self.path("manifest.txt"),
            "--markdown", md,
            cwd=self.tmp,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        text = self.read("complete.md").decode()
        self.assertIn("Headers with no executable lines (0/0)", text)
        self.assertIn("components/cn105/b.h", text)


if __name__ == "__main__":
    unittest.main()
