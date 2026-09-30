#!/usr/bin/env bash
# Entrypoint for the pebble-another-timer-tests image (see Containerfile's
# own comments for the full "why" on every design choice referenced here).
#
# Usage (via `podman run ... <image> <args>`):
#   <seq-file-relative-to-repo-root> [extra run_sequence.sh flags...]
#
# What this does, in order:
# 1. Copies the read-only /src mount (the repo, mounted by run_container.sh)
#    into a private, per-container scratch directory - never builds
#    in-place against /src, so N containers running in parallel against
#    the SAME host checkout never race on build/ output.
# 2. npm install + the copy's own tests/functional_framework/run_sequence.sh
#    handle the rest (pebble build is implicit in `pebble install`'s own
#    waf invocation, same as every other place in this project).
# 3. Wraps the actual run in libfaketime (see below) so the emulator's
#    displayed clock is deterministic across runs/containers without
#    needing --mask-rect or any emu-set-time pinning at all - live-
#    verified against this exact image: a real 5s timer's alarm screen
#    fired normally under this ("Timer A" / "+0:0N" / Keep running-+1 min-
#    Stop), NOT the "while your Pebble was off" system-notification bug
#    that pinning via `pebble emu-set-time` caused (see wipe_and_prep.seq's
#    own comment in the main framework for that history) - because
#    libfaketime shifts what time.time() returns for EVERY process in this
#    container consistently (pebble-tool's own post_connect() resync
#    included), so there's never a discontinuity between "what the tool
#    just pushed" and "what it pushes on the next connection", unlike
#    emu-set-time (which the AS-INSTALLED pypkjs.post_connect() overwrites
#    with the REAL host time on the very next TAP/APPMSG connection - see
#    that comment for the full mechanism).
#    Uses the MT (multi-threaded) build of libfaketime, not the plain one -
#    pypkjs is gevent-based (multi-greenlet); the plain build's caching
#    isn't documented as thread-safe and MT is the variant meant for this.
#
#    FAKETIME is set to a plain RELATIVE OFFSET in seconds (e.g. "-N"),
#    computed once below - deliberately NOT the "@2020-01-01 12:00:00"
#    absolute "start-at" form an earlier version of this script used.
#    live-verified those two forms behave very differently across the many
#    SEPARATE `pebble` CLI processes one test sequence spawns (screenshot/
#    send-app-message/emu-button/... each a fresh OS process): the "@"
#    form anchors its "keep advancing" reference to *when the affected
#    process itself started* (confirmed against libfaketime's own README:
#    "'start at' format allows a 'relative' clock operation ... using a
#    'start at' time instead of an offset time" - i.e. still fundamentally
#    process-start-relative), so a brand-new process reading a FAKETIME of
#    "@2020-01-01 12:00:00" reports a time close to that pinned instant
#    PLUS ITS OWN (tiny) uptime, near-ignoring how much real wall-clock
#    time has actually elapsed since the container/test run began. Since
#    pebble-tool's own post_connect() resyncs the emulator's clock (via a
#    SetUTC packet) to whatever time.time() says on EVERY new connection -
#    i.e. on every single separate `pebble` CLI call - this made the
#    emulator's clock repeatedly snap back near the pinned instant on
#    every command, discarding real elapsed time between commands
#    entirely. This was the actual root cause of a live-verified bug: the
#    alarm screen's live "+MM:SS" overtime counter (only meaningful in
#    alarm_overtime_display.seq, the one sequence that deliberately runs
#    with FreezeDisplay=0 to watch it tick) appeared to jump BACKWARD
#    between two screenshots instead of counting up, because each
#    screenshot's own resync was re-deriving "now" from ITS OWN process
#    start, not from shared elapsed real time.
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
#    needs real time to actually progress to run at all) - the same
#    "pin once, then tick, shared by everyone" semantic wipe_and_prep.seq's
#    old emu-set-time approach wanted, just without that approach's fatal
#    flaw (see the header comment above) AND without the "@" form's
#    per-process-anchor flaw discovered here. The 12:00 wall-clock time
#    deliberately matches copy_frozen_clock_string()'s own hardcoded
#    "12:00" (main.c) - so a screenshot's clock/bottom-bar reads the same
#    either way, whether FreezeDisplay pinned it directly or a sequence
#    (like alarm_overtime_display.seq) deliberately left it unfrozen to
#    watch real ticking. Not load-bearing for correctness (see
#    copy_frozen_clock_string()'s own comment on why the frozen value is
#    an arbitrary constant, not derived from anything) - purely to avoid a
#    confusing, unrelated-looking clock jump between otherwise-similar
#    screenshots depending on which sequence froze the display and which
#    didn't.
set -eu

SEQ_ARG="${1:?usage: <seq-file-relative-to-repo-root> [extra run_sequence.sh flags...]}"
shift

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
# (harmless in the current run_all_parallel.sh usage, where every job runs
# a distinct sequence name and so still gets a distinct RUN_ID/name path
# either way, but would collide if the same sequence were ever run twice
# concurrently).
#
# RUN_ID_OVERRIDE, if set, skips all of the above and is used as-is - set
# by run_container.sh when ITS caller (run_all_parallel.sh, for a shared
# batch run across many containers) exported it first: one shared host-
# generated timestamp (host clock, never touched by any container's own
# faketime) becomes every sequence's RUN_ID for that batch, so the whole
# batch's output lands under one shared $OUT_BASE/$RUN_ID/ directory - the
# same "one shared timestamp per batch" convention native run_all.sh used.
# Safe to share across containers in one batch specifically because each
# one runs a distinct sequence name (see the paragraph above) - no two
# ever write to the same $RUN_ID/$SEQ_NAME/ leaf path. Not set at all for
# a standalone single-sequence run (plain run_container.sh), where the
# generated-per-invocation RUN_ID below is exactly what's wanted instead.
# Also captured here, pre-LD_PRELOAD/FAKETIME, for the FAKETIME relative-
# offset computation below (see that comment for why a relative offset,
# not an absolute "@..." string, is what actually needs to be computed
# from a real, un-faked "now").
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
npm install --silent

export APP_TEST_HOOKS="${APP_TEST_HOOKS:-1}"

# This pebble-tool fork's `pebble install --emulator` does NOT auto-build
# on its own - live-verified: with no pre-existing build/, it fails
# outright ("You must either run this command from a project directory or
# specify the pbw to install.") rather than triggering waf itself the way
# CLAUDE.md's build-command notes describe. Build explicitly before
# handing off to run_sequence.sh, which - like every native .seq file in
# this project - only ever calls `pebble install` directly and relies on
# an implicit auto-build that doesn't actually happen in a from-scratch
# checkout (native runs this session likely got away with it only because
# a stale build/ from an earlier manual `pebble build` was already
# sitting there).
pebble build

export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/faketime/libfaketimeMT.so.1
export FAKETIME="$FAKETIME_OFFSET_SECONDS"

# Tells run_sequence.sh to drop a `.containerized` marker into this run's
# own output directory - the signal update_golden() (lib/golden.sh) checks
# before approving any golden baseline, since only this pinned-clock
# harness makes screenshots reproducible enough to serve as one (see
# tests/functional/docker/README.md and golden.sh's own comment).
export PEBBLE_TEST_CONTAINERIZED=1

# Opt-in only (set by run_container.sh's --touch flag, itself opt-in per
# invocation) - see Containerfile's own comment on run_touch/TOUCH for the
# full "why": a sequence using the TOUCH instruction needs a REAL windowed
# emulator (no --vnc) rendered into a virtual X display xdotool can target,
# which every other sequence has no need for and shouldn't pay the cost of
# (see tests/functional/docker/README.md's performance note). Xvfb's own
# startup is fast (live-verified near-instant - a 1s settle below is
# already generous headroom) but xdpyinfo is polled rather than assuming a
# fixed sleep is enough, since that startup time isn't guaranteed constant
# across hosts/load.
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

exec "$SCRATCH/tests/functional_framework/run_sequence.sh" \
  --conf "$SCRATCH/tests/functional/app.conf" \
  --seq "$SCRATCH/$SEQ_ARG" \
  --out-dir /out \
  --run-id "$RUN_ID" \
  "${EXTRA_RUN_SEQUENCE_ARGS[@]+"${EXTRA_RUN_SEQUENCE_ARGS[@]}"}" \
  "$@"
