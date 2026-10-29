#!/usr/bin/env bash
## ci_coverage_gate.sh — CI gate logic of the coverage-gate job (D-07, D-11..D-14)
## Role: re-proves the gate self-test, enforces the coverage ratchet, computes diff coverage of the
##       changed components/cn105 lines with the coverage-exempt label exemption, writes the job
##       summary and keeps one sticky PR comment. Runs in CI after tests/unit/coverage.sh --ci.
## Deps: bash, python3, git, diff-cover and gh (CI only); tests/unit/tools/coverage_gate.py
##
## Inputs come ONLY from environment variables (never from arguments or PR text):
##   EVENT_NAME   push | pull_request | workflow_dispatch
##   BASE_REF     pull request base branch (pull_request only)
##   PR_NUMBER    pull request number (pull_request only)
##   REPO         owner/name
##   SAME_REPO    "true" when the pull request head repository is REPO (comment allowed)
##   GH_TOKEN     repository GITHUB_TOKEN used by gh
##   RUN_URL      link to the workflow run, shown in the comment
##   OUT          report directory, default tests/unit/out
##   CN105_GATE_DRY_RUN=1  no GitHub API call; labels come from CN105_GATE_LABELS (comma-separated)
##                         and the comment body is printed to stdout
## Outputs in OUT: gate.md (ratchet), diff.md (diff coverage), comment.md (PR comment body),
##                 prev-baseline.json (baseline of the base branch, when it has one).
## Exit: 0 pass, 1 the ratchet or the diff rule failed (after summary and comment are written),
##       2 environment or input problem.
## This script never commits, pushes or edits repository content (D-12).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

MARKER='<!-- cn105-coverage-report -->'
TOOL=(python3 tests/unit/tools/coverage_gate.py)
OUT="${OUT:-tests/unit/out}"
EVENT_NAME="${EVENT_NAME:-}"
BASE_REF="${BASE_REF:-}"
PR_NUMBER="${PR_NUMBER:-}"
REPO="${REPO:-}"
SAME_REPO="${SAME_REPO:-false}"
RUN_URL="${RUN_URL:-}"
DRY_RUN="${CN105_GATE_DRY_RUN:-}"

die() {
    echo "[gate] $*" >&2
    exit 2
}

summary_append() {
    # Append a file to the GitHub job summary when running in CI.
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ] && [ -f "$1" ]; then
        cat "$1" >> "$GITHUB_STEP_SUMMARY"
        printf '\n' >> "$GITHUB_STEP_SUMMARY"
    fi
}

for needed in summary.json toolchain.json coverage.xml; do
    [ -f "$OUT/$needed" ] || die "missing $OUT/$needed (run tests/unit/coverage.sh --ci first)"
done

FAILED=0

# ── (a) gate self-test: re-proves the D-14 drop-fails / rise-ratchets demonstration ──
echo "[gate] self-test of coverage_gate.py"
python3 -m unittest discover -s tests/unit/tools -v || {
    echo "[gate] FAIL: the gate self-test failed" >&2
    exit 1
}

# ── (b) baseline of the base branch, used to refuse a silently lowered baseline (D-12) ──
PREV_ARGS=()
rm -f "$OUT/prev-baseline.json"
case "$EVENT_NAME" in
    pull_request)
        [ -n "$BASE_REF" ] || die "BASE_REF is required for pull_request"
        PREV_REF="origin/$BASE_REF"
        ;;
    push)
        PREV_REF="HEAD^1"
        ;;
    *)
        PREV_REF=""
        ;;
esac
if [ -n "$PREV_REF" ] && git show "$PREV_REF:tests/unit/coverage-baseline.json" > "$OUT/prev-baseline.json" 2>/dev/null; then
    PREV_ARGS=(--previous-baseline "$OUT/prev-baseline.json")
else
    rm -f "$OUT/prev-baseline.json"
    echo "[gate] no previous baseline on ${PREV_REF:-n/a}: lowered-baseline check skipped"
fi

# ── (c) ratchet ──
RATCHET_RC=0
"${TOOL[@]}" ratchet \
    --summary "$OUT/summary.json" \
    --baseline tests/unit/coverage-baseline.json \
    --toolchain "$OUT/toolchain.json" \
    "${PREV_ARGS[@]}" \
    --markdown "$OUT/gate.md" || RATCHET_RC=$?
case "$RATCHET_RC" in
    0) ;;
    1) FAILED=1 ;;
    *) die "coverage_gate.py ratchet failed with an input error (exit $RATCHET_RC)" ;;
esac
summary_append "$OUT/gate.md"

# ── (d) diff coverage of the changed lines, pull requests only ──
: > "$OUT/diff.md"
if [ "$EVENT_NAME" = "pull_request" ]; then
    LABELS=""
    if [ -n "$DRY_RUN" ]; then
        LABELS="$(printf '%s' "${CN105_GATE_LABELS:-}" | tr ',' '\n')"
    else
        [ -n "$REPO" ] && [ -n "$PR_NUMBER" ] || die "REPO and PR_NUMBER are required for pull_request"
        # The label is read live, at run time: adding it and re-running the job takes effect.
        if ! LABELS="$(gh api --paginate "repos/$REPO/issues/$PR_NUMBER/labels?per_page=100" --jq '.[].name')"; then
            echo "[gate] WARNING: could not read the PR labels; treating the PR as not exempt" >&2
            LABELS=""
        fi
    fi
    EXEMPT_ARGS=()
    if printf '%s\n' "$LABELS" | grep -qx 'coverage-exempt'; then
        EXEMPT_ARGS=(--exempt)
        echo "[gate] label coverage-exempt present: a failing diff coverage will not fail the job"
    fi

    diff-cover "$OUT/coverage.xml" \
        --compare-branch "origin/$BASE_REF" \
        --format "json:$OUT/diff-cover.json" || die "diff-cover failed (is origin/$BASE_REF fetched?)"

    DIFF_RC=0
    "${TOOL[@]}" diff \
        --report "$OUT/diff-cover.json" \
        --threshold 80 \
        "${EXEMPT_ARGS[@]}" \
        --markdown "$OUT/diff.md" || DIFF_RC=$?
    case "$DIFF_RC" in
        0) ;;
        1) FAILED=1 ;;
        *) die "coverage_gate.py diff failed with an input error (exit $DIFF_RC)" ;;
    esac
    summary_append "$OUT/diff.md"
fi

# ── (f) sticky PR comment: one comment carrying the marker, created then updated in place ──
if [ "$EVENT_NAME" = "pull_request" ]; then
    {
        printf '%s\n' "$MARKER"
        printf '## Coverage of components/cn105\n\n'
        for part in gcovr-summary.md scope.md gate.md diff.md; do
            if [ -s "$OUT/$part" ]; then
                cat "$OUT/$part"
                printf '\n'
            fi
        done
        if [ -n "$RUN_URL" ]; then
            printf 'Full report (HTML, Cobertura) in the `coverage-report` artifact of [this run](%s).\n' "$RUN_URL"
        fi
    } > "$OUT/comment.md"

    if [ -n "$DRY_RUN" ]; then
        echo "[gate] dry run: comment body follows"
        cat "$OUT/comment.md"
    elif [ "$SAME_REPO" = "true" ]; then
        # A comment problem never fails the job: it is a convenience, the job summary has the data.
        (
            COMMENTS_FILE="$OUT/pr-comments.jsonl"
            gh api --paginate "repos/$REPO/issues/$PR_NUMBER/comments?per_page=100" \
                --jq '.[] | {id: .id, body: .body}' > "$COMMENTS_FILE"
            TARGET="$("${TOOL[@]}" comment-target --comments "$COMMENTS_FILE" --marker "$MARKER")"
            if [ -n "$TARGET" ]; then
                gh api -X PATCH "repos/$REPO/issues/comments/$TARGET" -F "body=@$OUT/comment.md" > /dev/null
                echo "[gate] updated PR comment $TARGET"
            else
                gh api -X POST "repos/$REPO/issues/$PR_NUMBER/comments" -F "body=@$OUT/comment.md" > /dev/null
                echo "[gate] created PR comment"
            fi
        ) || echo "[gate] WARNING: could not create or update the PR comment" >&2
    else
        echo "[gate] pull request from a fork: no comment (read-only token); see the job summary"
    fi
fi

# ── (g) verdict, after summary and comment are written ──
if [ "$FAILED" -ne 0 ]; then
    echo "[gate] FAIL: the coverage ratchet or the diff coverage rule failed (see above)" >&2
    exit 1
fi
echo "[gate] OK"
