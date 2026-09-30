#!/usr/bin/env bash
# Runs every functional-test sequence under sequences/walkthroughs/ in one
# go, reporting a pass/fail summary. Thin, app-specific wrapper around
# tests/functional/docker/run_all_parallel.sh - see that script's own
# README (tests/functional/docker/README.md) for what containerized
# execution buys (parallelism, a deterministic emulator clock via
# libfaketime with none of the native emu-set-time approach's
# real-alarm-breaking side effects) and its one-time setup (build the
# image, size the Podman machine's memory).
#
# Usage:
#   tests/functional/run_all.sh [--golden-dir DIR] [--update-golden]
#                                [--fuzz PERCENT] [--mask-rect WxH+X+Y]
#                                [--continue-on-error]
#                                [--pattern GLOB] [-j N]
#
# --pattern restricts which sequences run, matched against each .seq
# file's basename (e.g. --pattern 'wakeup_conflict_*' for just that
# family) - default '*' (everything).
#
# --mask-rect has NO default here (unlike an earlier version of this
# script) - every sequence now freezes its own live displays deterministic
# via the FreezeDisplay/FreezeElapsedSeconds AppMessage fields
# (sequences/common/wipe_and_prep.seq sends them by default; see
# main.c's display_now()), so masking the bottom bar out of comparison is
# no longer needed at all. Pass --mask-rect explicitly only if some
# sequence shows a live region the freeze hook doesn't (yet) cover.
#
# CONTAINERIZED EXECUTION ONLY - THIS IS DELIBERATE, NOT A MISSING
# FEATURE. This script is the "does the suite pass" / CI / golden-approval
# entrypoint, and a native run's screenshots are not reproducible run to
# run (the emulator's displayed clock/elapsed counters depend on real
# wall-clock time with no pinning - see wipe_and_prep.seq's own comment
# for why pinning it natively was tried and reverted). Using native output
# here would make this script's own pass/fail verdict meaningless. If you
# want a fast single-sequence run for interactive dev/debugging (attaching
# VNC, poking at emulator state by hand, avoiding container image
# rebuilds), use tests/functional_framework/run_sequence.sh directly
# against your own host emulator instead - that native path still exists
# and is fully supported for that purpose, it's just never treated as
# authoritative for "does this pass" or as a source for approving a golden
# baseline (update_golden() in lib/golden.sh enforces this at the
# mechanism level too, independent of this script).
#
# -j N (default 4) sets how many containers run at once - see
# run_all_parallel.sh's own README section on Podman machine memory sizing
# before raising this.
#
# Failing sequences do NOT stop this script - it always runs every
# matching sequence and reports a summary at the end, exiting non-zero
# only if at least one sequence failed. (--continue-on-error, if given,
# is passed through to each individual run too, so a failed STEP within
# one sequence doesn't even stop that one sequence early - a different
# granularity from this script's own always-continue behavior across
# sequences.)
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARALLEL="$SCRIPT_DIR/docker/run_all_parallel.sh"

usage() {
  sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
}

PATTERN='*'
JOBS=""
PASSTHROUGH=()

while [ $# -gt 0 ]; do
  case "$1" in
    --golden-dir) PASSTHROUGH+=(--golden-dir "$2"); shift 2 ;;
    --update-golden) PASSTHROUGH+=(--update-golden); shift ;;
    --fuzz) PASSTHROUGH+=(--fuzz "$2"); shift 2 ;;
    --mask-rect) PASSTHROUGH+=(--mask-rect "$2"); shift 2 ;;
    --continue-on-error) PASSTHROUGH+=(--continue-on-error); shift ;;
    --pattern) PATTERN="$2"; shift 2 ;;
    -j) JOBS="$2"; shift 2 ;;
    --containers)
      # No longer meaningful (this script IS the containerized runner now)
      # - accepted as a harmless no-op so an existing script/alias that
      # still passes it doesn't break.
      shift
      ;;
    --vnc|--no-vnc)
      echo "$1 is meaningless here - containerized runs always use --vnc internally. If you wanted a native run, call tests/functional_framework/run_sequence.sh directly instead (see this script's own --help for why run_all.sh itself no longer supports native mode)." >&2
      exit 1
      ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

if [ ! -x "$PARALLEL" ]; then
  echo "Container runner not found or not executable: $PARALLEL" >&2
  exit 1
fi

CONTAINER_ARGS=(--pattern "$PATTERN")
[ -n "$JOBS" ] && CONTAINER_ARGS+=(-j "$JOBS")
exec "$PARALLEL" "${CONTAINER_ARGS[@]}" ${PASSTHROUGH[@]+"${PASSTHROUGH[@]}"}
