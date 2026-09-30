#!/usr/bin/env bash
# Promotes an ALREADY-PRODUCED run_sequence.sh output directory to the
# golden (approved) baseline, without re-running anything against the
# emulator.
#
# run_sequence.sh --golden-dir DIR --update-golden does the same
# underlying copy (see lib/golden.sh's update_golden), but only as the
# last step of a fresh live run - useful as the common case, but wasteful
# when you already have a passing run's output sitting in out/ (a live
# run here means a full WIPE+INSTALL+button/AppMessage walkthrough, easily
# 30-90s+ per sequence) and just want to approve THAT output. This script
# is the same operation, decoupled from execution: point it at an
# existing <out-dir>/<RUN_ID>/<sequence-name>/ directory and it copies
# that directory's screenshots + scrubbed run.log into the golden dir,
# exactly as --update-golden would have.
#
# REQUIRES a run produced via tests/functional/docker/ (run_container.sh /
# run_all_parallel.sh), NOT a native run - update_golden() (lib/golden.sh)
# checks for a `.containerized` marker file in --run-dir and refuses
# otherwise, since only the containerized harness's pinned libfaketime
# clock makes screenshots reproducible enough to serve as a baseline. See
# tests/functional/docker/README.md.
#
# Usage:
#   promote_golden.sh --run-dir DIR --golden-dir DIR [--seq-name NAME] [--force]
#
# Options:
#   --run-dir DIR      A run's own output directory (contains run.log and
#                       any NN_label.png screenshots) - what run_sequence.sh
#                       printed as "output dir: ..." for the run you want
#                       to promote.
#   --golden-dir DIR    Base golden directory (same meaning as
#                       run_sequence.sh's --golden-dir) - the per-sequence
#                       subdirectory is created/overwritten under it.
#   --seq-name NAME     Sequence name to promote as (the golden
#                       subdirectory becomes <golden-dir>/<NAME>/). Default:
#                       the basename of --run-dir itself, since
#                       run_sequence.sh's own layout is always
#                       <out-dir>/<RUN_ID>/<sequence-name>/ - only needed
#                       if promoting from a differently-laid-out directory.
#   --force             Promote even if run.log doesn't end with "sequence
#                       PASSED" (e.g. a run captured with
#                       --continue-on-error that had a failed step, or a
#                       log that's been hand-edited/truncated). Without
#                       this, such a run-dir is refused - promoting a
#                       failing or inconclusive run's output as the
#                       approved baseline is very likely a mistake.
#   -h, --help          Show this help.
set -u

FRAMEWORK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/log.sh
source "$FRAMEWORK_DIR/lib/log.sh"
# shellcheck source=lib/golden.sh
source "$FRAMEWORK_DIR/lib/golden.sh"

# log_info/log_error (lib/log.sh) tee to $LOG_FILE - this script doesn't
# produce its own run log (it isn't a run), so discard that side of it and
# rely on their stdout/stderr echo alone.
LOG_FILE=/dev/null

usage() {
  sed -n '2,36p' "$0" | sed 's/^# \{0,1\}//'
}

RUN_DIR=""
GOLDEN_DIR=""
SEQ_NAME=""
FORCE=0

while [ $# -gt 0 ]; do
  case "$1" in
    --run-dir) RUN_DIR="$2"; shift 2 ;;
    --golden-dir) GOLDEN_DIR="$2"; shift 2 ;;
    --seq-name) SEQ_NAME="$2"; shift 2 ;;
    --force) FORCE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

if [ -z "$RUN_DIR" ] || [ -z "$GOLDEN_DIR" ]; then
  echo "Both --run-dir and --golden-dir are required." >&2
  usage
  exit 1
fi
if [ ! -d "$RUN_DIR" ]; then
  echo "Run directory not found: $RUN_DIR" >&2
  exit 1
fi
if [ ! -f "$RUN_DIR/run.log" ]; then
  echo "$RUN_DIR does not look like a run_sequence.sh output directory (no run.log)." >&2
  exit 1
fi

if [ -z "$SEQ_NAME" ]; then
  SEQ_NAME="$(basename "$(cd "$RUN_DIR" && pwd)")"
fi

if [ "$FORCE" != "1" ] && ! grep -q "sequence PASSED" "$RUN_DIR/run.log"; then
  echo "refusing to promote $RUN_DIR: run.log does not end with 'sequence PASSED'" >&2
  echo "(pass --force to promote anyway - e.g. for a --continue-on-error run with a known-acceptable failure)" >&2
  exit 1
fi

GOLDEN_SEQ_DIR="$GOLDEN_DIR/$SEQ_NAME"
if ! update_golden "$RUN_DIR" "$GOLDEN_SEQ_DIR"; then
  exit 1
fi
log_info "promoted $RUN_DIR -> $GOLDEN_SEQ_DIR (no emulator run performed)"
