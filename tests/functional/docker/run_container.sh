#!/usr/bin/env bash
# Runs ONE functional-test sequence inside the pebble-another-timer-tests
# container image (see Containerfile). Build the image first:
#   podman build --platform linux/amd64 -t pebble-another-timer-tests \
#     -f tests/functional/docker/Containerfile .
#
# Usage:
#   tests/functional/docker/run_container.sh <seq-file> [--touch] [extra run_sequence.sh flags...]
#
# <seq-file> may be given relative to the repo root or as an absolute path
# under the repo - either way it's translated to a path relative to the
# repo root, since that's what the container sees mounted at /src.
#
# --touch: opt-in only, needed for a sequence using the TOUCH instruction
# (see functional_framework/README.md and this dir's own README.md's
# performance note) - starts Xvfb and runs the emulator without --vnc
# inside the container instead of the default --vnc-only path every other
# sequence uses.
#
# --init is required (see Containerfile's own comment on why - a real init
# process as PID 1 is what lets pebble-tool's own kill/wipe/install cycle
# work correctly across more than one call in the same container).
set -eu

SEQ_ARG="${1:?usage: run_container.sh <seq-file> [extra run_sequence.sh flags...]}"
shift

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE="${PEBBLE_TEST_IMAGE:-pebble-another-timer-tests}"

# Accept an absolute path under the repo (what a human tab-completes to)
# as well as a repo-relative one (what run_all_parallel.sh passes).
case "$SEQ_ARG" in
  "$REPO_ROOT"/*) SEQ_REL="${SEQ_ARG#"$REPO_ROOT"/}" ;;
  *) SEQ_REL="$SEQ_ARG" ;;
esac

mkdir -p "$REPO_ROOT/tests/functional/out"

# --golden-dir's host path isn't visible inside the container by default -
# mount it at a fixed internal path and rewrite the flag's value to match,
# so golden comparison/--update-golden works the same as it does natively.
# On macOS, this path must be somewhere Podman's own VM actually shares
# (in practice, somewhere under $HOME - this repo's own tests/functional/
# golden/ qualifies) - live-verified that a bare /tmp/... path fails with
# `Error: statfs ...: no such file or directory` at podman-run time, since
# /tmp isn't in the applehv machine's default shared-mount scope. Not
# something this script can fix or detect in advance; if you hit that
# error, move --golden-dir's target under $HOME.
EXTRA_ARGS=()
GOLDEN_MOUNT=()
TOUCH_ENV=()
while [ $# -gt 0 ]; do
  case "$1" in
    --golden-dir)
      GOLDEN_HOST="$2"
      mkdir -p "$GOLDEN_HOST"
      GOLDEN_HOST="$(cd "$GOLDEN_HOST" && pwd)"
      GOLDEN_MOUNT=(-v "$GOLDEN_HOST:/golden")
      EXTRA_ARGS+=(--golden-dir /golden)
      shift 2
      ;;
    # Opt-in: only sequences using the TOUCH instruction need this - it
    # starts Xvfb and runs the emulator without --vnc inside the container
    # (see run-sequence-in-container.sh and Containerfile's own comments
    # for the full "why"). Consumed here, not forwarded to run_sequence.sh
    # as a real flag - it's a container-level concern, not one of its own.
    --touch) TOUCH_ENV=(-e "PEBBLE_TEST_TOUCH=1"); shift ;;
    *) EXTRA_ARGS+=("$1"); shift ;;
  esac
done

# If our own caller (run_all_parallel.sh, for a shared batch run) exported
# RUN_ID_OVERRIDE, forward it into the container so this run lands under
# that shared output directory instead of generating its own - see
# run-sequence-in-container.sh's own comment for why that generation
# normally has to happen post-faketime and can't just be `date` inside the
# container. Not set at all for a standalone single-sequence invocation of
# this script, which is exactly when the container's own generated RUN_ID
# (unique per invocation) is what you want anyway.
RUN_ID_ENV=()
[ -n "${RUN_ID_OVERRIDE:-}" ] && RUN_ID_ENV=(-e "RUN_ID_OVERRIDE=$RUN_ID_OVERRIDE")

podman run --rm --init --platform linux/amd64 \
  -v "$REPO_ROOT":/src:ro \
  -v "$REPO_ROOT/tests/functional/out":/out \
  "${GOLDEN_MOUNT[@]+"${GOLDEN_MOUNT[@]}"}" \
  "${RUN_ID_ENV[@]+"${RUN_ID_ENV[@]}"}" \
  "${TOUCH_ENV[@]+"${TOUCH_ENV[@]}"}" \
  "$IMAGE" "$SEQ_REL" "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"
