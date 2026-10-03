#!/usr/bin/env bash
# Thin, app-specific shim over tests/functional_framework/run_sequence.sh,
# pinning --conf to this project's own app.conf so every existing
# `tests/functional/run_sequence.sh <seq-file> [flags...]` invocation (and
# CLAUDE.md's own documented usage) keeps working unchanged. See
# tests/functional_framework/run_sequence.sh --help for the full flag
# reference (--container/--no-container, --golden-dir, --touch, ...).
#
# This project's own app.conf defaults to CONTAINER=1 (see container/
# README.md under tests/functional_framework/ for the full "why" -
# deterministic clock, parallel-safe), so a plain
# `tests/functional/run_sequence.sh <seq-file>` already runs containerized
# and needs the image built first:
#   tests/functional_framework/container/build_image.sh --conf tests/functional/app.conf
# Pass --no-container for a fast native run against an already-running
# emulator instead - fully supported for interactive dev/debugging, but
# never trust a golden comparison/approval made that way (see CLAUDE.md's
# own caveat; update_golden() refuses it anyway).
#
# Usage:
#   tests/functional/run_sequence.sh <seq-file> [flags...]
#   tests/functional/run_sequence.sh --build-only DIR
#
# <seq-file> may be given relative to the repo root, relative to the
# caller's own cwd, or as an absolute path - resolved against the repo
# root here so this works the same regardless of where you run it from.
#
# This project's own policy: screenshots are always compared exactly
# (fuzz=0) - every sequence already makes its own live displays
# deterministic via app-side test hooks or per-screenshot masking (see
# tests/functional_framework/README.md's SCREENSHOT row) rather than
# papering over drift with fuzz, so --fuzz is rejected here rather than
# forwarded (the underlying framework still supports it generically for a
# project that wants it).
set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CONF_FILE="$SCRIPT_DIR/app.conf"
FRAMEWORK="$REPO_ROOT/tests/functional_framework/run_sequence.sh"

if [ "${1:-}" = "--build-only" ]; then
  DIR="${2:?usage: run_sequence.sh --build-only <output-dir>}"
  exec "$FRAMEWORK" --conf "$CONF_FILE" --build-only "$DIR"
fi

case "${1:-}" in
  -h|--help) exec "$FRAMEWORK" --help ;;
esac

SEQ_ARG="${1:?usage: run_sequence.sh <seq-file> [flags...] (--help for details)}"
shift

# Accept an absolute path under the repo (what a human tab-completes to),
# one relative to the repo root (what run_batch.sh passes), or one relative
# to the caller's own cwd.
case "$SEQ_ARG" in
  "$REPO_ROOT"/*) : ;;
  /*) SEQ_ARG="$SEQ_ARG" ;;
  *)
    if [ -f "$REPO_ROOT/$SEQ_ARG" ]; then
      SEQ_ARG="$REPO_ROOT/$SEQ_ARG"
    fi
    ;;
esac

for a in "$@"; do
  case "$a" in
    --fuzz) echo "--fuzz is not supported: this project always compares exactly (fuzz=0) - see this script's own header comment." >&2; exit 1 ;;
  esac
done

exec "$FRAMEWORK" --conf "$CONF_FILE" --seq "$SEQ_ARG" "$@" --fuzz 0
