#!/usr/bin/env bash
## coverage.sh — the single coverage entry point for components/cn105 (D-08)
## Role: builds the -m32 coverage build, runs every test configuration (pure tests, ESP32 and ESP8266
##       characterization), and writes line + branch coverage reports (Markdown, HTML, Cobertura, JSON)
##       to the out directory. Local developers and CI run this same script, so their numbers agree.
## Deps: bash, cmake, g++ with -m32 support (or docker + tests/unit/docker/run.sh), python3 with venv,
##       gcovr 8.6 and diff-cover 10.6.0 (installed into a virtualenv from requirements-coverage.lock.txt),
##       tests/unit/tools/coverage_gate.py
##
## Usage: tests/unit/coverage.sh [--docker] [--ci] [--build-dir DIR] [--out DIR]
##   --docker      re-run this script inside the ubuntu:24.04 + g++-multilib dev image
##   --ci          skip the virtualenv bootstrap; gcovr and diff-cover must already be on PATH
##   --build-dir   CMake build directory, default tests/unit/build-cov
##   --out         report directory, default tests/unit/out
## Outputs (in --out): tf_esp32.json tf_esp8266.json tf_pure.json summary.json gcovr-summary.md
##                     coverage.xml html/index.html toolchain.json compiled-headers.txt complete.md
##                     scope.md gate.md gate.json report.md
## Exit codes: 0 ok; 1 build or test failure; 2 environment problem (no -m32, wrong tool version,
##             no virtualenv support, bad usage); 3 scope failure (a components/cn105 .cpp or .h is
##             missing from the report, or a header has no header-TU .gcno in a variant).
set -euo pipefail

# The repo root comes from this script's location, never from git: inside the Docker image a git
# worktree's .git link points outside the mount.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Object sets: one gcovr tracefile each. The cn105_headers_<v> directories hold the .gcno of the
# forced-emission header translation units (never linked, so no .gcda): gcovr reads them as zero counts.
HARNESS_VARIANTS="esp32 esp8266"

USE_DOCKER=0
CI_MODE=0
BUILD_DIR="$SCRIPT_DIR/build-cov"
OUT_DIR="$SCRIPT_DIR/out"
ORIGINAL_ARGS=("$@")

usage_error() {
    echo "[coverage] $1" >&2
    echo "usage: tests/unit/coverage.sh [--docker] [--ci] [--build-dir DIR] [--out DIR]" >&2
    exit 2
}

abs_path() {
    case "$1" in
        /*) printf '%s' "$1" ;;
        *) printf '%s' "$REPO_ROOT/$1" ;;
    esac
}

while [ $# -gt 0 ]; do
    case "$1" in
        --docker) USE_DOCKER=1 ;;
        --ci) CI_MODE=1 ;;
        --build-dir) [ $# -ge 2 ] || usage_error "--build-dir needs a value"; BUILD_DIR="$(abs_path "$2")"; shift ;;
        --out) [ $# -ge 2 ] || usage_error "--out needs a value"; OUT_DIR="$(abs_path "$2")"; shift ;;
        *) usage_error "unknown argument: $1" ;;
    esac
    shift
done

if [ "$USE_DOCKER" -eq 1 ]; then
    # Re-execute inside the dev image, without --docker, keeping every other argument as given.
    INNER=("tests/unit/coverage.sh")
    for arg in "${ORIGINAL_ARGS[@]}"; do
        [ "$arg" = "--docker" ] || INNER+=("$arg")
    done
    printf -v INNER_CMD '%q ' "${INNER[@]}"
    exec "$SCRIPT_DIR/docker/run.sh" "$INNER_CMD"
fi

# ── Tool bootstrap ──────────────────────────────────────────────────────────
if [ "$CI_MODE" -eq 0 ]; then
    if [ "${CN105_IN_DOCKER:-0}" = "1" ]; then
        VENV="$SCRIPT_DIR/.venv-coverage-docker"
    else
        VENV="$SCRIPT_DIR/.venv-coverage"
    fi
    if [ ! -x "$VENV/bin/pip" ]; then
        if ! python3 -m venv "$VENV" >/dev/null 2>&1; then
            rm -rf "$VENV"
            echo "[coverage] cannot create a virtualenv (install python3-venv) or use --docker" >&2
            exit 2
        fi
    fi
    # Wheels only: a source distribution would run its setup.py at install time (T-01-07).
    REQ_FILE="$SCRIPT_DIR/requirements-coverage.lock.txt"
    [ -f "$REQ_FILE" ] || REQ_FILE="$SCRIPT_DIR/requirements-coverage.txt"
    if ! "$VENV/bin/pip" install --quiet --require-virtualenv --only-binary=:all: -r "$REQ_FILE"; then
        echo "[coverage] pip install from $(basename "$REQ_FILE") failed" >&2
        exit 2
    fi
    export PATH="$VENV/bin:$PATH"
fi

if ! command -v gcovr >/dev/null 2>&1; then
    echo "[coverage] gcovr is not on PATH (run without --ci to bootstrap a virtualenv)" >&2
    exit 2
fi
GCOVR_VERSION="$(gcovr --version | head -n 1 | sed -n 's/^gcovr \([0-9][0-9.]*\).*/\1/p')"
if [ "$GCOVR_VERSION" != "8.6" ]; then
    echo "[coverage] gcovr 8.6 is required, found '${GCOVR_VERSION:-unknown}'" >&2
    exit 2
fi

# A working -m32 toolchain is required (D-20); without it the CMake try-compile would fail late.
PROBE_DIR="$(mktemp -d)"
trap 'rm -rf "$PROBE_DIR"' EXIT
printf 'int main() { return 0; }\n' > "$PROBE_DIR/probe.cpp"
if ! g++ -m32 "$PROBE_DIR/probe.cpp" -o "$PROBE_DIR/probe" >/dev/null 2>&1; then
    echo "[coverage] g++ -m32 cannot link a program on this machine: install g++-multilib or use --docker" >&2
    exit 2
fi

GATE=(python3 "$SCRIPT_DIR/tools/coverage_gate.py")
mkdir -p "$OUT_DIR"

# ── Build and run every test configuration ─────────────────────────────────
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 -DCN105_COVERAGE=ON >/dev/null
if ! cmake --build "$BUILD_DIR" -j"$(nproc)"; then
    echo "[coverage] build failed" >&2
    exit 1
fi

# Counts from a previous run would add to this run's: start from zero.
find "$BUILD_DIR" -name '*.gcda' -delete

TEST_STATUS=0
(cd "$BUILD_DIR" && ctest --output-on-failure --no-tests=error) || TEST_STATUS=$?

# ── Per-object-set tracefiles ──────────────────────────────────────────────
# Filter: every file under components/cn105 stays in (D-10); nothing of it is excluded. The
# --exclude-directory pattern only matches CMake's versioned compiler-id directories
# (CMakeFiles/<cmake version>/), never a target object directory such as cn105_headers_esp32.dir.
CN105_FILTER="$REPO_ROOT/components/cn105/"
COMMON_GCOVR=(--root "$REPO_ROOT" --filter "$CN105_FILTER"
    --exclude-directory '.*/CMakeFiles/[0-9][0-9.]*$'
    --exclude-throw-branches --exclude-unreachable-branches)

run_tracefile() {
    local name="$1"
    shift
    gcovr "${COMMON_GCOVR[@]}" --json "$OUT_DIR/tf_${name}.json" "$@"
}

for v in $HARNESS_VARIANTS; do
    run_tracefile "$v" \
        "$BUILD_DIR/harness/CMakeFiles/cn105_${v}.dir" \
        "$BUILD_DIR/harness/CMakeFiles/cn105_headers_${v}.dir" \
        "$BUILD_DIR/characterization/CMakeFiles/cn105_char_${v}.dir"
done
run_tracefile pure \
    "$BUILD_DIR/CMakeFiles/cn105_tests.dir" \
    "$BUILD_DIR/CMakeFiles/cn105_legacy_action_tests.dir"

# ── Merged reports ─────────────────────────────────────────────────────────
rm -rf "$OUT_DIR/html"
mkdir -p "$OUT_DIR/html"
MERGE_ARGS=()
for name in $HARNESS_VARIANTS pure; do
    MERGE_ARGS+=(-a "$OUT_DIR/tf_${name}.json")
done
gcovr --root "$REPO_ROOT" --filter "$CN105_FILTER" "${MERGE_ARGS[@]}" \
    --json-summary "$OUT_DIR/summary.json" --json-summary-pretty \
    --markdown-summary "$OUT_DIR/gcovr-summary.md" \
    --cobertura "$OUT_DIR/coverage.xml" --cobertura-pretty \
    --html-nested "$OUT_DIR/html/index.html"

# ── Toolchain record ───────────────────────────────────────────────────────
GCC_VERSION="$(gcc -dumpfullversion)"
ESPHOME_TAG="$(sed -n 's/^[[:space:]]*GIT_TAG[[:space:]]\{1,\}\([^[:space:]]*\).*/\1/p' "$SCRIPT_DIR/harness/CMakeLists.txt" | head -n 1)"
OS_ID="$(. /etc/os-release && printf '%s %s' "$ID" "$VERSION_ID")"
printf '{\n  "gcc": "%s",\n  "gcovr": "%s",\n  "esphome": "%s",\n  "os": "%s"\n}\n' \
    "$GCC_VERSION" "$GCOVR_VERSION" "$ESPHOME_TAG" "$OS_ID" > "$OUT_DIR/toolchain.json"

# ── Header completion, scope and ratchet (D-10) ────────────────────────────
# compiled-headers.txt lists a header only when its generated TU left <header>.cpp.gcno in BOTH
# variants; a header missing from either variant is left out, which fails the scope check below.
# (The file also lists the .cpp objects, see the loop after this one.)
: > "$OUT_DIR/compiled-headers.txt"
for header in "$REPO_ROOT"/components/cn105/*.h; do
    name="$(basename "$header")"
    present=1
    for v in $HARNESS_VARIANTS; do
        if [ -z "$(find "$BUILD_DIR/harness/CMakeFiles/cn105_headers_${v}.dir" -name "${name}.cpp.gcno" -print -quit 2>/dev/null)" ]; then
            present=0
        fi
    done
    if [ "$present" -eq 1 ]; then
        echo "components/cn105/$name" >> "$OUT_DIR/compiled-headers.txt"
    else
        echo "[coverage] header $name has no header-TU .gcno in every variant" >&2
    fi
done

# A .cpp with no executable line at all (include and comment only, e.g. info_request.cpp) leaves a
# .gcno but no gcov record; listing the .cpp objects of both variants lets `complete` report it 0/0.
for source in "$REPO_ROOT"/components/cn105/*.cpp; do
    name="$(basename "$source")"
    present=1
    for v in $HARNESS_VARIANTS; do
        if [ -z "$(find "$BUILD_DIR/harness/CMakeFiles/cn105_${v}.dir" -name "${name}.gcno" -print -quit 2>/dev/null)" ]; then
            present=0
        fi
    done
    if [ "$present" -eq 1 ]; then
        echo "components/cn105/$name" >> "$OUT_DIR/compiled-headers.txt"
    else
        echo "[coverage] source $name has no object .gcno in every variant" >&2
    fi
done

"${GATE[@]}" complete --summary "$OUT_DIR/summary.json" --cobertura "$OUT_DIR/coverage.xml" \
    --source-dir components/cn105 --compiled-headers "$OUT_DIR/compiled-headers.txt" \
    --markdown "$OUT_DIR/complete.md"

SCOPE_STATUS=0
(cd "$REPO_ROOT" && "${GATE[@]}" scope --summary "$OUT_DIR/summary.json" \
    --source-dir components/cn105 --markdown "$OUT_DIR/scope.md") || SCOPE_STATUS=$?

GATE_STATUS=0
"${GATE[@]}" ratchet --summary "$OUT_DIR/summary.json" --baseline "$SCRIPT_DIR/coverage-baseline.json" \
    --toolchain "$OUT_DIR/toolchain.json" --markdown "$OUT_DIR/gate.md" --json "$OUT_DIR/gate.json" \
    || GATE_STATUS=$?

{
    echo "## CN105 coverage (components/cn105)"
    echo
    cat "$OUT_DIR/gcovr-summary.md"
    echo
    cat "$OUT_DIR/complete.md"
    echo
    cat "$OUT_DIR/scope.md"
    echo
    cat "$OUT_DIR/gate.md"
} > "$OUT_DIR/report.md"

echo "[coverage] report: $OUT_DIR/report.md  html: $OUT_DIR/html/index.html"

if [ "$TEST_STATUS" -ne 0 ]; then
    echo "[coverage] tests failed (ctest exit $TEST_STATUS); reports were still written" >&2
    exit 1
fi
if [ "$SCOPE_STATUS" -ne 0 ]; then
    echo "[coverage] scope check failed (exit $SCOPE_STATUS): see $OUT_DIR/scope.md" >&2
    exit 3
fi
exit 0
