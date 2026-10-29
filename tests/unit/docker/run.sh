#!/usr/bin/env bash
## run.sh — run a command inside the ubuntu:24.04 + g++-multilib dev image
## Role: lets a developer machine without g++-multilib build and run the -m32 harness (D-20).
## Deps: docker; tests/unit/docker/Dockerfile
##
## Usage: tests/unit/docker/run.sh '<shell command>'
## The repository is mounted at the same absolute path inside the container so that CMake
## caches and gcov paths stay valid on the host and in the container.
set -euo pipefail

# Derive the repository root from this script's own location, never from git: inside the
# container a git worktree's .git link points outside the mount.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"

HASH="$(sha256sum "$SCRIPT_DIR/Dockerfile" | cut -c1-12)"
IMAGE="cn105-unit-env:ubuntu24.04-${HASH}"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build -t "$IMAGE" -f "$SCRIPT_DIR/Dockerfile" "$SCRIPT_DIR"
fi

ARGS=(run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e CN105_IN_DOCKER=1)

# Offline overrides, passed through only when set.
if [ -n "${FETCHCONTENT_SOURCE_DIR_ESPHOME:-}" ]; then
    ARGS+=(-e FETCHCONTENT_SOURCE_DIR_ESPHOME -v "${FETCHCONTENT_SOURCE_DIR_ESPHOME}:${FETCHCONTENT_SOURCE_DIR_ESPHOME}:ro")
fi
if [ -n "${FETCHCONTENT_SOURCE_DIR_GOOGLETEST:-}" ]; then
    ARGS+=(-e FETCHCONTENT_SOURCE_DIR_GOOGLETEST -v "${FETCHCONTENT_SOURCE_DIR_GOOGLETEST}:${FETCHCONTENT_SOURCE_DIR_GOOGLETEST}:ro")
fi

ARGS+=(-v "$REPO:$REPO" -w "$REPO")

# A git worktree has a .git *file* pointing at the common dir: mount that dir too so git and
# diff-cover keep working inside the container.
if [ -f "$REPO/.git" ]; then
    COMMON="$(cd "$REPO" && git rev-parse --git-common-dir)"
    case "$COMMON" in
        /*) ;;
        *) COMMON="$(cd "$REPO/$COMMON" && pwd)" ;;
    esac
    ARGS+=(-v "$COMMON:$COMMON")
fi

exec docker "${ARGS[@]}" "$IMAGE" bash -c "$*"
