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
#   tests/functional/run_sequence.sh <seq-file> [flags...]
#   tests/functional/run_sequence.sh --build-only DIR
#
# <seq-file> may be given relative to the repo root or as an absolute path
# under the repo - either way it's resolved against the repo root for
# native mode, and translated to a path relative to the repo root for
# container mode (what the container sees mounted at /src).
#
# Flags (this wrapper's own):
#
#   --no-container          Run natively against a shared host emulator
#                            instead of inside the container image. Fast
#                            for interactive dev/debugging, but its
#                            screenshots aren't reproducible run to run -
#                            never trust a golden comparison/approval made
#                            this way (see --golden-dir below and
#                            CLAUDE.md's own caveat).
#
#   --golden-dir DIR         Base dir of golden baselines, one
#                            subdirectory per sequence, to compare this
#                            run's screenshots against (or approve into,
#                            with --update-golden). Default:
#                            tests/functional/golden (this project's own,
#                            committed baseline set) - override only to
#                            compare/approve against some other location.
#                            Forwarded to functional_framework/
#                            run_sequence.sh; in container mode, the host
#                            path is mounted into the container at a fixed
#                            internal path first - on macOS this must be
#                            somewhere Podman's own VM actually shares (in
#                            practice, somewhere under $HOME - this repo's
#                            own tests/functional/golden/ qualifies); a
#                            bare /tmp/... path fails at podman-run time
#                            with `Error: statfs ...: no such file or
#                            directory`, since /tmp isn't in the applehv
#                            machine's default shared-mount scope.
#
#   --touch                  Starts Xvfb and runs the emulator without
#                            --vnc inside the container instead of the
#                            default --vnc-only path every other sequence
#                            uses (see functional_framework/README.md and
#                            container/README.md's performance note) -
#                            needed for a sequence using the TOUCH/
#                            TOUCHDOWN/TOUCHUP/TOUCHMOVE/TOUCHSWEEP/
#                            TOUCHDRAG instructions. Auto-detected by
#                            default (including through IMPORTs) in
#                            container mode - passing it explicitly is
#                            only needed to force touch mode on a sequence
#                            this detection doesn't catch. INCOMPATIBLE
#                            WITH --no-container: a real touchscreen event
#                            only reaches the guest through a genuine
#                            SDL/X11 window an Xvfb-backed xdotool can
#                            target, which native mode has no supported
#                            way to provide (qemu's --vnc framebuffer
#                            never delivers touch input, and moving the
#                            real OS cursor/granting Accessibility
#                            permissions to automate a native desktop is
#                            undesirable - see container/Containerfile's
#                            own comment) - rejected outright rather than
#                            left to fail confusingly partway through a
#                            run.
#
#   --prebuilt-build DIR     Skip this container's own npm install +
#                            pebble build and reuse the build/ tree a
#                            prior --build-only DIR run already produced,
#                            mounted read-only. DIR must be the SAME
#                            directory passed to that --build-only call.
#                            Container-only (rejected with
#                            --no-container, which has no per-container
#                            build to skip in the first place). Exists for
#                            run_all.sh's batch use (build once, reuse
#                            across every sequence container in the
#                            batch), not meant for standalone use.
#
#   -h, --help               Show this help.
#
# Any other flag (e.g. --out-dir, --run-id, --continue-on-error, --vnc/
# --no-vnc, --update-golden, --mask-rect) is forwarded as-is to
# tests/functional_framework/run_sequence.sh - see its own --help for the
# full reference. --fuzz is the one exception: this script always forwards
# --fuzz 0 (exact match) itself and does not accept it as a flag - every
# sequence already makes its own live displays deterministic via app-side
# test hooks or per-screenshot masking (see tests/functional_framework/
# README.md's SCREENSHOT row) rather than papering over drift with fuzz;
# same reasoning and the same fixed value as run_all.sh's own "no --fuzz
# flag, by design".
#
# --build-only DIR: build once (npm install + pebble build) inside the
# same image every sequence container uses, and copy the result to
# DIR/build on the host, instead of running any sequence. Runs no
# sequence at all - just builds. Used by run_all.sh (see its own comment)
# to build ONCE for a whole batch instead of once per sequence container -
# a standalone single-sequence run has no such batch to amortize a shared
# build across, so this isn't meant to be used outside of run_all.sh.
#
# --init is required for container mode (see Containerfile's own comment
# on why - a real init process as PID 1 is what lets pebble-tool's own
# kill/wipe/install cycle work correctly across more than one call in the
# same container).
set -eu

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE="${PEBBLE_TEST_IMAGE:-pebble-another-timer-tests}"

usage() {
  sed -n '2,121p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

# --build-only DIR: see this script's own header comment for what/why.
# Checked before anything else since it's a separate single-purpose mode,
# not one of the flags in the loop below - it takes the place of
# <seq-file> entirely rather than modifying a run. See also
# container/run-sequence-in-container.sh's own --build-only handling.
if [ "${1:-}" = "--build-only" ]; then
  OUT_BUILD_DIR="${2:?usage: run_sequence.sh --build-only <output-dir>}"
  mkdir -p "$OUT_BUILD_DIR"
  OUT_BUILD_DIR="$(cd "$OUT_BUILD_DIR" && pwd)"
  exec podman run --rm --init --platform linux/amd64 \
    -v "$REPO_ROOT":/src:ro \
    -v "$OUT_BUILD_DIR":/out-build \
    "$IMAGE" --build-only
fi

case "${1:-}" in
  -h|--help) usage; exit 0 ;;
esac

SEQ_ARG="${1:?usage: run_sequence.sh <seq-file> [flags...] (--help for details)}"
shift

# Accept an absolute path under the repo (what a human tab-completes to)
# as well as a repo-relative one (what run_all.sh passes).
case "$SEQ_ARG" in
  "$REPO_ROOT"/*) SEQ_REL="${SEQ_ARG#"$REPO_ROOT"/}" ;;
  *) SEQ_REL="$SEQ_ARG" ;;
esac

mkdir -p "$REPO_ROOT/tests/functional/out"

CONTAINER=1
EXTRA_ARGS=()
GOLDEN_MOUNT=()
TOUCH_ENV=()
TOUCH_REQUESTED=0
PREBUILT_MOUNT=()
PREBUILT_ENV=()
GOLDEN_HOST="$REPO_ROOT/tests/functional/golden"
while [ $# -gt 0 ]; do
  case "$1" in
    --no-container) CONTAINER=0; shift ;;
    --golden-dir) GOLDEN_HOST="$2"; shift 2 ;;
    # Deliberately unsupported here - see this script's own header comment
    # on why --fuzz is always forced to 0 below rather than exposed as a
    # flag (same reasoning/value as run_all.sh's own "no --fuzz flag, by
    # design").
    --fuzz) echo "--fuzz is not supported: this script always compares exactly (fuzz=0) - see its --help." >&2; exit 1 ;;
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
    # Set by run_all.sh (DIR == the same dir it passed to --build-only
    # earlier for this batch) when it already built once for the whole
    # batch - mounts that build read-only and tells the container's own
    # entrypoint to reuse it instead of repeating npm install + pebble
    # build (see container/run-sequence-in-container.sh's PREBUILT_BUILD
    # handling). Container-only - validated against --no-container below,
    # once argument parsing is done (so either flag order works, same
    # reasoning as --touch above).
    --prebuilt-build) PREBUILT_BUILD_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    # Flags belonging to functional_framework/run_sequence.sh itself
    # (--out-dir, --run-id, --continue-on-error, --vnc/--no-vnc,
    # --update-golden, --mask-rect, ...) - forwarded through unchanged
    # rather than duplicated here, so this wrapper doesn't have to track
    # the framework's own flag list. Anything else is a typo, not a
    # forwardable flag - reject it immediately instead of silently
    # passing it through and letting the container fail late and
    # confusingly (as a bare --help once did here before this check
    # existed).
    --out-dir|--run-id|--mask-rect) EXTRA_ARGS+=("$1" "$2"); shift 2 ;;
    --vnc|--no-vnc|--update-golden|--continue-on-error) EXTRA_ARGS+=("$1"); shift ;;
    -*) echo "Unknown argument: $1" >&2; usage >&2; exit 1 ;;
    *) EXTRA_ARGS+=("$1"); shift ;;
  esac
done

# Always exact (fuzz=0) - see this script's own header comment and the
# --fuzz case above.
EXTRA_ARGS+=(--fuzz 0)

# --golden-dir's host path isn't visible inside the container by default -
# mount it at a fixed internal path and rewrite the flag's value to match
# (see this script's own header comment for the macOS/Podman-VM mount-
# scope caveat this depends on). Native mode needs none of this -
# --golden-dir is forwarded to functional_framework/run_sequence.sh
# unchanged, since it already runs directly against the host filesystem.
# Applied here, after the flag loop, using whatever GOLDEN_HOST ended up
# as (the default set above, or an explicit --golden-dir override) - so
# this doesn't matter which order --golden-dir and --no-container were
# given in.
if [ "$CONTAINER" = "1" ]; then
  mkdir -p "$GOLDEN_HOST"
  GOLDEN_HOST="$(cd "$GOLDEN_HOST" && pwd)"
  GOLDEN_MOUNT=(-v "$GOLDEN_HOST:/golden")
  EXTRA_ARGS+=(--golden-dir /golden)
else
  EXTRA_ARGS+=(--golden-dir "$GOLDEN_HOST")
fi

if [ -n "${PREBUILT_BUILD_DIR:-}" ]; then
  if [ "$CONTAINER" != "1" ]; then
    echo "--prebuilt-build is incompatible with --no-container: there's no per-container build to skip in native mode." >&2
    exit 1
  fi
  PREBUILT_HOST="$(cd "$PREBUILT_BUILD_DIR/build" && pwd)"
  PREBUILT_MOUNT=(-v "$PREBUILT_HOST:/prebuilt-build:ro")
  PREBUILT_ENV=(-e "PREBUILT_BUILD=1")
fi

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
  "${PREBUILT_MOUNT[@]+"${PREBUILT_MOUNT[@]}"}" \
  "${PREBUILT_ENV[@]+"${PREBUILT_ENV[@]}"}" \
  "$IMAGE" "$SEQ_REL" "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"
