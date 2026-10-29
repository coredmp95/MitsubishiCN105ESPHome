#!/usr/bin/env bash
## run-harness.sh — build and run the characterization harness binaries (one per variant)
## Role: the single local entry point for the -m32 characterization suites (COV-01, COV-04). It
##       configures tests/unit with -m32, builds only the cn105_char_<variant> targets (plus the
##       coverage-only header objects with --coverage), checks that the gtest filter selects at least
##       one test per variant and runs the selected tests. No g++-multilib on the host: use --docker.
## Deps: bash, cmake, a C++ toolchain with -m32 support (or docker + tests/unit/docker/run.sh)
##
## Usage: tests/unit/run-harness.sh [--docker] [--variant esp32|esp8266|all] [--filter <gtest filter>]
##                                  [--build-dir <dir>] [--coverage]
##   --docker      re-run this script inside the ubuntu:24.04 + g++-multilib dev image
##   --variant     one variant, or "all" (every entry of HARNESS_VARIANTS), default all
##   --filter      GoogleTest filter, default '*'; the filter is applied to the suite.test names
##                 as GoogleTest prints them (no variant prefix: "HarnessSmoke.*", not "Esp32.*")
##   --build-dir   CMake build directory, default tests/unit/build-m32
##                 (tests/unit/build-cov when --coverage is given without --build-dir)
##   --coverage    configure with -DCN105_COVERAGE=ON (gcov instrumentation of components/cn105, the
##                 pure tests and the characterization objects, plus one coverage-only translation
##                 unit per components/cn105 header)
## Exit codes: 0 every selected variant ran >= 1 test and all passed; 1 a test or build failed;
##             2 native -m32 is unavailable (use --docker); 4 a filter selected 0 tests in a variant;
##             64 usage error.
set -euo pipefail

# Variants known to the harness; plan 01-03 adds esp8266 here.
HARNESS_VARIANTS="esp32"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

USE_DOCKER=0
VARIANT="all"
FILTER="*"
BUILD_DIR=""
COVERAGE=0
ORIGINAL_ARGS=("$@")

usage_error() {
    echo "[run-harness] $1" >&2
    echo "usage: tests/unit/run-harness.sh [--docker] [--variant esp32|esp8266|all] [--filter <gtest filter>] [--build-dir <dir>] [--coverage]" >&2
    exit 64
}

while [ $# -gt 0 ]; do
    case "$1" in
        --docker) USE_DOCKER=1 ;;
        --coverage) COVERAGE=1 ;;
        --variant) [ $# -ge 2 ] || usage_error "--variant needs a value"; VARIANT="$2"; shift ;;
        --filter) [ $# -ge 2 ] || usage_error "--filter needs a value"; FILTER="$2"; shift ;;
        --build-dir) [ $# -ge 2 ] || usage_error "--build-dir needs a value"; BUILD_DIR="$2"; shift ;;
        -h|--help) sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | grep '^##' | sed 's/^## \{0,1\}//'; exit 0 ;;
        *) usage_error "unknown argument: $1" ;;
    esac
    shift
done

if [ "$USE_DOCKER" -eq 1 ]; then
    # Re-execute inside the dev image, without --docker, keeping every other argument as given.
    INNER=("tests/unit/run-harness.sh")
    for arg in "${ORIGINAL_ARGS[@]}"; do
        [ "$arg" = "--docker" ] || INNER+=("$arg")
    done
    printf -v INNER_CMD '%q ' "${INNER[@]}"
    exec "$SCRIPT_DIR/docker/run.sh" "$INNER_CMD"
fi

if [ "$VARIANT" = "all" ]; then
    VARIANTS="$HARNESS_VARIANTS"
else
    case " $HARNESS_VARIANTS " in
        *" $VARIANT "*) VARIANTS="$VARIANT" ;;
        *) usage_error "unknown variant '$VARIANT' (available: $HARNESS_VARIANTS)" ;;
    esac
fi

if [ -z "$BUILD_DIR" ]; then
    if [ "$COVERAGE" -eq 1 ]; then BUILD_DIR="tests/unit/build-cov"; else BUILD_DIR="tests/unit/build-m32"; fi
fi
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$REPO_ROOT/$BUILD_DIR" ;;
esac

# Native mode needs a working -m32 toolchain (D-20); without it the CMake try-compile would fail late.
PROBE_DIR="$(mktemp -d)"
trap 'rm -rf "$PROBE_DIR"' EXIT
printf 'int main() { return 0; }\n' > "$PROBE_DIR/probe.cpp"
if ! g++ -m32 "$PROBE_DIR/probe.cpp" -o "$PROBE_DIR/probe" >/dev/null 2>&1; then
    echo "[run-harness] g++ -m32 cannot link a program on this machine: install g++-multilib or use --docker" >&2
    exit 2
fi

CMAKE_ARGS=(-S "$REPO_ROOT/tests/unit" -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32)
if [ "$COVERAGE" -eq 1 ]; then
    CMAKE_ARGS+=(-DCN105_COVERAGE=ON)
else
    CMAKE_ARGS+=(-DCN105_COVERAGE=OFF)
fi
cmake "${CMAKE_ARGS[@]}" >/dev/null

TARGETS=()
for v in $VARIANTS; do
    TARGETS+=("cn105_char_$v")
    if [ "$COVERAGE" -eq 1 ]; then TARGETS+=("cn105_headers_$v"); fi
done
cmake --build "$BUILD_DIR" --target "${TARGETS[@]}" -j"$(nproc)"

# --coverage reruns would add counts to stale .gcda files of a previous run: start from zero.
if [ "$COVERAGE" -eq 1 ]; then
    find "$BUILD_DIR" -name '*.gcda' -delete
fi

for v in $VARIANTS; do
    BIN="$BUILD_DIR/characterization/cn105_char_$v"
    SELECTED="$("$BIN" --gtest_list_tests --gtest_filter="$FILTER" | grep -c '^  ' || true)"
    echo "[run-harness] $v: $SELECTED tests selected"
    if [ "$SELECTED" -eq 0 ]; then
        echo "[run-harness] filter '$FILTER' selects no test in variant $v" >&2
        exit 4
    fi
    # Declaration order, no --gtest_shuffle: HarnessIsolation relies on it.
    "$BIN" --gtest_filter="$FILTER"
done

echo "[run-harness] OK: $VARIANTS"
