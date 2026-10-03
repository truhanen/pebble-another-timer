#!/usr/bin/env bash
# Entrypoint for the functional_framework test-runner image (see
# Containerfile's own comments for the full "why" on every design choice
# referenced here, and container/README.md for the full picture). This
# script runs INSIDE the container; container/launch.sh is what starts it
# from the host side.
#
# App-agnostic: everything app-specific it needs (which conf/seq file to
# run, what to export before building) is passed in via argv/env by
# launch.sh, not hardcoded here.
#
# Usage (via `podman run ... <image> <args>`, always through launch.sh):
#   <conf-file-relative-to-repo-root> <seq-file-relative-to-repo-root> \
#     [extra run_sequence.sh flags...]
#   Requires FRAMEWORK_REL (env) - functional_framework's own directory,
#   relative to the repo root - set by launch.sh (which gets it from
#   run_sequence.sh, the only thing that actually knows where it's
#   installed in a given project) - so this script never has to hardcode
#   that path itself.
#   --build-only
#     Builds once (npm install + pebble build) and copies the resulting
#     build/ tree out to /out-build (must be mounted writable by the
#     caller), then exits - runs no sequence at all. Used by run_batch.sh's
#     batch mode to build ONCE for a whole batch instead of once per
#     sequence container - see its own comment on PREBUILT_BUILD below for
#     the other half of this mechanism.
#
# What this does, in order:
# 1. Copies the read-only /src mount (the repo, mounted by
#    container/launch.sh) into a private, per-container scratch directory -
#    never builds in-place against /src, so N containers running in
#    parallel against the SAME host checkout never race on build/ output.
# 2. npm install + pebble build produce build/ in that scratch directory -
#    UNLESS the caller already built once for this whole batch and mounted
#    the result read-only at /prebuilt-build (PREBUILT_BUILD=1 - see
#    run_batch.sh), in which case that's copied into place instead, skipping
#    npm install/pebble build here entirely. Either way, the copy's own
#    run_sequence.sh handles the rest from there.
# 3. Wraps the actual run in libfaketime (see below) so the emulator's
#    displayed clock is deterministic across runs/containers without
#    needing --mask-rect or any emu-set-time pinning at all - live-
#    verified: a real short timer's alarm screen fired normally under
#    this, NOT the "while your Pebble was off" system-notification bug
#    that pinning via `pebble emu-set-time` can cause - because
#    libfaketime shifts what time.time() returns for EVERY process in this
#    container consistently (pebble-tool's own post_connect() resync
#    included), so there's never a discontinuity between "what the tool
#    just pushed" and "what it pushes on the next connection", unlike
#    emu-set-time (which an as-installed pypkjs's post_connect()
#    overwrites with the REAL host time on the very next TAP/APPMSG
#    connection).
#    Uses the MT (multi-threaded) build of libfaketime, not the plain one -
#    pypkjs is gevent-based (multi-greenlet); the plain build's caching
#    isn't documented as thread-safe and MT is the variant meant for this.
#
#    FAKETIME is set to a plain RELATIVE OFFSET in seconds (e.g. "-N"),
#    computed once below - deliberately NOT an "@2020-01-01 12:00:00"
#    absolute "start-at" form. Live-verified those two forms behave very
#    differently across the many SEPARATE `pebble` CLI processes one test
#    sequence spawns (screenshot/send-app-message/emu-button/... each a
#    fresh OS process): the "@" form anchors its "keep advancing"
#    reference to *when the affected process itself started* (confirmed
#    against libfaketime's own README: "'start at' format allows a
#    'relative' clock operation ... using a 'start at' time instead of an
#    offset time" - i.e. still fundamentally process-start-relative), so a
#    brand-new process reading a FAKETIME of "@2020-01-01 12:00:00"
#    reports a time close to that pinned instant PLUS ITS OWN (tiny)
#    uptime, near-ignoring how much real wall-clock time has actually
#    elapsed since the container/test run began. Since pebble-tool's own
#    post_connect() resyncs the emulator's clock (via a SetUTC packet) to
#    whatever time.time() says on EVERY new connection - i.e. on every
#    single separate `pebble` CLI call - this made the emulator's clock
#    repeatedly snap back near the pinned instant on every command,
#    discarding real elapsed time between commands entirely. This was the
#    actual root cause of a live-verified bug: a live "+MM:SS"-style
#    overtime/elapsed counter (only meaningful in a sequence that
#    deliberately never freezes its own display, to watch it tick)
#    appeared to jump BACKWARD between two screenshots instead of counting
#    up, because each screenshot's own resync was re-deriving "now" from
#    ITS OWN process start, not from shared elapsed real time.
#
#    A plain relative offset with no "@" does not have this problem -
#    per libfaketime's own README, "libfaketime then will always report
#    the faked time based on the real current time and the offset you've
#    specified", i.e. evaluated fresh against the REAL system clock on
#    every single query, by every process, with no per-process anchor -
#    so independently-spawned processes sharing the same FAKETIME offset
#    always agree with each other AND correctly reflect true elapsed real
#    time, without needing FAKETIME_TIMESTAMP_FILE/FAKETIME_NO_CACHE or
#    any other cross-process synchronization machinery at all.
#
#    The offset is computed to land near 2020-01-01 12:00:00 at the moment
#    this script starts, then correctly ticks forward from there at real
#    speed for the rest of the container's life (not frozen - qemu-pebble
#    needs real time to actually progress to run at all) - "pin once, then
#    tick, shared by everyone". Not load-bearing for correctness (any
#    stable instant would do) - a fixed, readable reference just avoids a
#    confusing, unrelated-looking clock jump between otherwise-similar
#    screenshots depending on whether a given sequence freezes its own
#    display or deliberately leaves it live to watch real ticking.
set -eu

# --build-only: produce a shared build/ tree and exit - no sequence runs,
# no faketime, none of the rest of this file applies. See this file's own
# header comment and run_batch.sh's for the full mechanism.
if [ "${1:-}" = "--build-only" ]; then
  SCRATCH="$(mktemp -d /tmp/pebble-proj.XXXXXX)"
  cp -r /src/. "$SCRATCH/"
  cd "$SCRATCH"
  npm install --silent
  # BUILD_ENV, if set (by launch.sh, from the project's own app.conf), is a
  # newline/space-separated list of KEY=VAL pairs to export before
  # building - e.g. a project's own test-hooks build flag. App-agnostic:
  # this script has no opinion on what's in it.
  if [ -n "${BUILD_ENV:-}" ]; then
    while IFS= read -r kv; do
      [ -n "$kv" ] && export "${kv?}"
    done <<EOF
$BUILD_ENV
EOF
  fi
  pebble build
  # waf's app_bundle task names the output .pbw after the CURRENT project
  # directory's own basename (live-verified: a /tmp/pebble-proj.XXXXXX
  # build produces build/pebble-proj.XXXXXX.pbw) - `pebble install` with
  # no explicit path expects a .pbw matching ITS OWN CWD's basename, which
  # every consuming container's scratch dir has a different (random) name
  # for. Rename to a fixed, known name here so the consuming side (see
  # PREBUILT_BUILD below) can deterministically rename it again to match
  # ITS OWN scratch dir, regardless of what this build's own scratch dir
  # happened to be called.
  mv "$SCRATCH/build/$(basename "$SCRATCH").pbw" "$SCRATCH/build/shared.pbw"
  cp -r "$SCRATCH/build" /out-build/
  exit 0
fi

CONF_REL="${1:?usage: <conf-file-relative-to-repo-root> <seq-file-relative-to-repo-root> [extra run_sequence.sh flags...]}"
SEQ_REL="${2:?usage: <conf-file-relative-to-repo-root> <seq-file-relative-to-repo-root> [extra run_sequence.sh flags...]}"
shift 2

# Captured BEFORE LD_PRELOAD/FAKETIME are set below, so this reflects the
# REAL host/container time, not the frozen fake one - run_sequence.sh's
# own RUN_ID generation (`date +%Y%m%d_%H%M%S`) would otherwise run under
# faketime too and return the exact SAME value on every single container
# invocation (always "20200101_120000"), making every separate run's
# output directory collide on one shared path/log file - live-verified,
# this is exactly what happened before this fix (two unrelated runs'
# run.log content interleaved in one file). Passed through explicitly via
# --run-id so run_sequence.sh never calls `date` itself once faketime is
# active.
#
# The suffix is the mktemp-generated scratch dir's own random suffix, not
# $$ - every container gets its own PID namespace, so the entrypoint
# script commonly gets the SAME low PID (e.g. 2) in every container;
# live-verified two parallel containers landing on the same real second
# AND the same PID, making $$ useless for cross-container uniqueness here
# (harmless as long as every job in one batch runs a distinct sequence
# name, which run_batch.sh guarantees, but would collide if the same
# sequence were ever run twice concurrently).
#
# RUN_ID_OVERRIDE, if set, skips all of the above and is used as-is - set
# by container/launch.sh when ITS caller (run_batch.sh, for a shared batch
# run across many containers) exported it first: one shared host-generated
# timestamp (host clock, never touched by any container's own faketime)
# becomes every sequence's RUN_ID for that batch, so the whole batch's
# output lands under one shared $OUT_BASE/$RUN_ID/ directory. Safe to share
# across containers in one batch specifically because each one runs a
# distinct sequence name - no two ever write to the same
# $RUN_ID/$SEQ_NAME/ leaf path. Not set at all for a standalone
# single-sequence run, where the generated-per-invocation RUN_ID below is
# exactly what's wanted instead. Also captured here, pre-LD_PRELOAD/
# FAKETIME, for the FAKETIME relative-offset computation below (see that
# comment for why a relative offset, not an absolute "@..." string, is
# what actually needs to be computed from a real, un-faked "now").
REAL_NOW_EPOCH="$(date -u +%s)"
FAKETIME_TARGET_EPOCH="$(date -u -d '2020-01-01 12:00:00' +%s)"
FAKETIME_OFFSET_SECONDS=$((FAKETIME_TARGET_EPOCH - REAL_NOW_EPOCH))
if [ "$FAKETIME_OFFSET_SECONDS" -ge 0 ]; then
  FAKETIME_OFFSET_SECONDS="+${FAKETIME_OFFSET_SECONDS}"
fi

SCRATCH="$(mktemp -d /tmp/pebble-proj.XXXXXX)"
RUN_ID="${RUN_ID_OVERRIDE:-$(date '+%Y%m%d_%H%M%S')_$(basename "$SCRATCH")}"
cp -r /src/. "$SCRATCH/"
cd "$SCRATCH"

if [ -n "${BUILD_ENV:-}" ]; then
  while IFS= read -r kv; do
    [ -n "$kv" ] && export "${kv?}"
  done <<EOF
$BUILD_ENV
EOF
fi

if [ "${PREBUILT_BUILD:-0}" = "1" ]; then
  # run_batch.sh already built ONCE for this whole batch (--build-only, same
  # image, same source, same BUILD_ENV - so identical output regardless of
  # which container produces it) and mounted the result read-only at
  # /prebuilt-build - reuse it instead of repeating npm install + pebble
  # build in every single sequence container. See run_batch.sh's own comment
  # for the time savings this buys.
  #
  # rm -rf first: the /src copy above may have brought along a stale
  # build/ of its own (e.g. a leftover from a native `pebble build` run
  # directly against the real checkout) - start clean so only the
  # prebuilt one's contents end up here, not some merge of both.
  rm -rf "$SCRATCH/build"
  cp -r /prebuilt-build "$SCRATCH/build"
  # See --build-only's own comment above for why this rename is needed:
  # `pebble install` (no explicit path) expects a .pbw matching THIS
  # container's own scratch dir basename, not whichever one originally
  # produced the shared build.
  cp "$SCRATCH/build/shared.pbw" "$SCRATCH/build/$(basename "$SCRATCH").pbw"
else
  npm install --silent
  # Some pebble-tool forks' `pebble install --emulator` do NOT auto-build
  # on their own - with no pre-existing build/, that fails outright rather
  # than triggering waf itself. Build explicitly before handing off to
  # run_sequence.sh, which only ever calls `pebble install` directly and
  # relies on an implicit auto-build that may not actually happen in a
  # from-scratch checkout.
  pebble build
fi

export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/faketime/libfaketimeMT.so.1
export FAKETIME="$FAKETIME_OFFSET_SECONDS"

# Tells run_sequence.sh to drop a `.containerized` marker into this run's
# own output directory - the signal update_golden() (lib/golden.sh) checks
# before approving any golden baseline, since only this pinned-clock
# harness makes screenshots reproducible enough to serve as one (see
# container/README.md and golden.sh's own comment).
export PEBBLE_TEST_CONTAINERIZED=1

# Opt-in only (set by container/launch.sh when run_sequence.sh's --touch
# flag, itself opt-in per invocation, requested it - see Containerfile's
# own comment on xvfb/xdotool for the full "why"): a sequence using the
# TOUCH instruction needs a REAL windowed emulator (no --vnc) rendered
# into a virtual X display xdotool can target, which every other sequence
# has no need for and shouldn't pay the cost of (see container/README.md's
# performance note). Xvfb's own startup is fast (live-verified
# near-instant) but xdpyinfo is polled rather than assuming a fixed sleep
# is enough, since that startup time isn't guaranteed constant across
# hosts/load.
EXTRA_RUN_SEQUENCE_ARGS=()
if [ "${PEBBLE_TEST_TOUCH:-0}" = "1" ]; then
  export DISPLAY=:99
  Xvfb "$DISPLAY" -screen 0 400x400x24 >/tmp/xvfb.log 2>&1 &
  for _ in $(seq 1 50); do
    xdpyinfo >/dev/null 2>&1 && break
    sleep 0.1
  done
  if ! xdpyinfo >/dev/null 2>&1; then
    echo "Xvfb did not come up in time - see /tmp/xvfb.log" >&2
    cat /tmp/xvfb.log >&2 || true
    exit 1
  fi
  # TOUCH requires a real SDL/X11 window (see Containerfile) - --no-vnc
  # overrides app.conf's VNC=1 default for this run regardless of what the
  # sequence's own conf says, since qemu's -vnc framebuffer never delivers
  # touch input to the guest at all.
  EXTRA_RUN_SEQUENCE_ARGS+=(--no-vnc)
fi

: "${FRAMEWORK_REL:?FRAMEWORK_REL must be set (by launch.sh) to the functional_framework directory path relative to the repo root}"

exec "$SCRATCH/$FRAMEWORK_REL/run_sequence.sh" \
  --conf "$SCRATCH/$CONF_REL" \
  --seq "$SCRATCH/$SEQ_REL" \
  --out-dir /out \
  --run-id "$RUN_ID" \
  "${EXTRA_RUN_SEQUENCE_ARGS[@]+"${EXTRA_RUN_SEQUENCE_ARGS[@]}"}" \
  "$@"
