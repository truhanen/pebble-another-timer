#!/usr/bin/env bash
# Runs ONE functional-test sequence for this app - inside the
# pebble-another-timer-tests container image (see container/Containerfile)
# by default, or natively against a shared host emulator with
# --no-container. This is the one entrypoint to use directly;
# tests/functional_framework/run_sequence.sh (the generic, app-agnostic
# engine both modes call into) shouldn't need to be invoked by hand for
# this app anymore.
#
# Container mode (the default) needs the image built first:
#   podman build --platform linux/amd64 -t pebble-another-timer-tests \
#     -f tests/functional/container/Containerfile .
# See container/README.md for why this is also the REQUIRED path for
# golden-baseline approval/CI (reproducible clock, no shared-emulator
# races). --no-container needs no such setup beyond what the rest of
# CLAUDE.md already describes (a built app installed to a running
# emulator, same as any other native .seq run - see the pebble-emulator
# skill) - it's the right choice for fast interactive dev/debugging
# against an already-running emulator, at the cost of non-reproducible
# screenshots.
#
# Usage:
#   tests/functional/run_sequence.sh <seq-file> [--no-container] [--touch] [extra flags...]
#
# <seq-file> may be given relative to the repo root or as an absolute path
# under the repo - either way it's resolved against the repo root for
# native mode, and translated to a path relative to the repo root for
# container mode (what the container sees mounted at /src).
#
# --no-container: run natively against a shared host emulator instead of
# inside the container image.
#
# --touch: starts Xvfb and runs the emulator without --vnc inside the
# container instead of the default --vnc-only path every other sequence
# uses (see functional_framework/README.md and container/README.md's
# performance note) - needed for a sequence using the TOUCH/TOUCHDOWN/
# TOUCHUP/TOUCHMOVE/TOUCHSWEEP/TOUCHDRAG instructions. Auto-detected by
# default (including through IMPORTs) in container mode - passing it
# explicitly is only needed to force touch mode on a sequence this
# detection doesn't catch. INCOMPATIBLE WITH --no-container: a real
# touchscreen event only reaches the guest through a genuine SDL/X11
# window an Xvfb-backed xdotool can target, which native mode has no
# supported way to provide (qemu's --vnc framebuffer never delivers touch
# input, and moving the real OS cursor/granting Accessibility permissions
# to automate a native desktop is undesirable - see
# container/Containerfile's own comment) - rejected outright below rather
# than left to fail confusingly partway through a run.
#
# --init is required for container mode (see Containerfile's own comment
# on why - a real init process as PID 1 is what lets pebble-tool's own
# kill/wipe/install cycle work correctly across more than one call in the
# same container).
set -eu

SEQ_ARG="${1:?usage: run_sequence.sh <seq-file> [--no-container] [--touch] [extra flags...]}"
shift

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE="${PEBBLE_TEST_IMAGE:-pebble-another-timer-tests}"

# Accept an absolute path under the repo (what a human tab-completes to)
# as well as a repo-relative one (what run_all.sh passes).
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
# error, move --golden-dir's target under $HOME. Native mode needs none of
# this - --golden-dir is forwarded to functional_framework/run_sequence.sh
# unchanged, since it already runs directly against the host filesystem.
CONTAINER=1
EXTRA_ARGS=()
GOLDEN_MOUNT=()
TOUCH_ENV=()
TOUCH_REQUESTED=0
while [ $# -gt 0 ]; do
  case "$1" in
    --no-container) CONTAINER=0; shift ;;
    --golden-dir)
      GOLDEN_HOST="$2"
      if [ "$CONTAINER" = "1" ]; then
        mkdir -p "$GOLDEN_HOST"
        GOLDEN_HOST="$(cd "$GOLDEN_HOST" && pwd)"
        GOLDEN_MOUNT=(-v "$GOLDEN_HOST:/golden")
        EXTRA_ARGS+=(--golden-dir /golden)
      else
        EXTRA_ARGS+=(--golden-dir "$GOLDEN_HOST")
      fi
      shift 2
      ;;
    # Opt-in: only sequences using the TOUCH instruction need this - it
    # starts Xvfb and runs the emulator without --vnc inside the container
    # (see container/run-sequence-in-container.sh and container/Containerfile's
    # own comments for the full "why") - needed for a sequence using the
    # TOUCH/TOUCHDOWN/TOUCHUP/TOUCHMOVE/TOUCHSWEEP/TOUCHDRAG instructions.
    # Consumed here, not forwarded to functional_framework/run_sequence.sh
    # as a real flag - it's a container-level concern, not one of its
    # own. Validated against --no-container below, once argument parsing
    # is done (so --touch before --no-container on the command line still
    # works).
    --touch) TOUCH_REQUESTED=1; shift ;;
    *) EXTRA_ARGS+=("$1"); shift ;;
  esac
done

if [ "$TOUCH_REQUESTED" = "1" ]; then
  if [ "$CONTAINER" != "1" ]; then
    echo "--touch is incompatible with --no-container: a real touchscreen event only reaches the guest through a genuine SDL/X11 window (Xvfb+xdotool, container-only) - qemu's --vnc framebuffer never delivers touch input, and there is no supported native equivalent (see container/Containerfile's own comment)." >&2
    exit 1
  fi
  TOUCH_ENV=(-e "PEBBLE_TEST_TOUCH=1")
fi

if [ "$CONTAINER" != "1" ]; then
  exec "$REPO_ROOT/tests/functional_framework/run_sequence.sh" \
    --conf "$REPO_ROOT/tests/functional/app.conf" \
    --seq "$REPO_ROOT/$SEQ_REL" \
    "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"
fi

# Auto-detect sequences that actually need --touch (any TOUCH/TOUCHDOWN/
# TOUCHUP/TOUCHMOVE/TOUCHSWEEP/TOUCHDRAG instruction - see
# functional_framework/README.md's instruction-set reference), including
# through IMPORTs, instead of requiring every caller to remember and pass
# --touch by hand. An explicit --touch above still wins (this only fires
# if nothing already set TOUCH_ENV) - this is purely a default, not the
# only way to request touch mode. This is also why these sequences used
# to silently error out of a batch `run_all.sh` run: nothing passed
# --touch for them, and the error was otherwise easy to miss scrolling
# past in parallel output.
if [ "${#TOUCH_ENV[@]}" -eq 0 ]; then
  FRAMEWORK_DIR="$REPO_ROOT/tests/functional_framework"
  LOG_FILE=/dev/null
  # shellcheck source=../functional_framework/lib/log.sh
  . "$FRAMEWORK_DIR/lib/log.sh"
  # shellcheck source=../functional_framework/lib/parse.sh
  . "$FRAMEWORK_DIR/lib/parse.sh"
  if flatten_sequence "$REPO_ROOT/$SEQ_REL" "" 2>/dev/null \
      | cut -f2- | awk '{print $1}' | grep -q '^TOUCH'; then
    TOUCH_ENV=(-e "PEBBLE_TEST_TOUCH=1")
  fi
fi

# If our own caller (run_all.sh, for a shared batch run) exported
# RUN_ID_OVERRIDE, forward it into the container so this run lands under
# that shared output directory instead of generating its own - see
# container/run-sequence-in-container.sh's own comment for why that
# generation normally has to happen post-faketime and can't just be
# `date` inside the container. Not set at all for a standalone
# single-sequence invocation of this script, which is exactly when the
# container's own generated RUN_ID (unique per invocation) is what you
# want anyway.
RUN_ID_ENV=()
[ -n "${RUN_ID_OVERRIDE:-}" ] && RUN_ID_ENV=(-e "RUN_ID_OVERRIDE=$RUN_ID_OVERRIDE")

podman run --rm --init --platform linux/amd64 \
  -v "$REPO_ROOT":/src:ro \
  -v "$REPO_ROOT/tests/functional/out":/out \
  "${GOLDEN_MOUNT[@]+"${GOLDEN_MOUNT[@]}"}" \
  "${RUN_ID_ENV[@]+"${RUN_ID_ENV[@]}"}" \
  "${TOUCH_ENV[@]+"${TOUCH_ENV[@]}"}" \
  "$IMAGE" "$SEQ_REL" "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"
