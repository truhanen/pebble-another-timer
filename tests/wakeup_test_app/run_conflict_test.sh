#!/usr/bin/env bash
# Drives a real (non-hack) cross-app wakeup-conflict test end to end, as a
# single script instead of interactive tool calls - see the
# wakeup_conflict_remaining_sites memory note this recipe came from. The
# actual blocker to landing this test manually was per-call tool round-trip
# latency (~10-12s per interactive `pebble` call in that environment), not
# anything about the app; running the whole sequence as one local script
# removes that tax and should make the tight ±60s exclusion-window timing
# land reliably.
#
# What it does:
#   1. Wipes the emulator for a deterministic starting state.
#   2. Installs pebble-another-timer, disables its idle-exit.
#   3. Installs wakeup_test_app, schedules a real wakeup WAKEUP_OFFSET
#      seconds out via AppMessage, then lets it auto-exit to the watchface
#      (it always does, ~2s after scheduling - that's the whole point: it
#      has to leave the foreground for its own wakeup to be able to
#      pre-empt anything later).
#   4. Switches to pebble-another-timer, creates a new timer with duration
#      REAL_DURATION_S seconds (chosen to end shortly AFTER
#      WAKEUP_OFFSET - see the tuning note below), which should hit a REAL
#      conflict (not TEMP-TEST-FORCE) against the wakeup from step 3.
#   5. Selects "Keep app in foreground" on that conflict.
#   6. Polls with periodic screenshots until either the wakeup fires
#      (pre-empting the app - the watchface/launcher reappears) or the
#      timer's own natural end passes (tick_cb catches it directly, no
#      wakeup involved) - the interesting outcome is the FORMER, since
#      that's the "another app's wakeup pre-empts a still-running unresolved
#      conflict" scenario the whole tool exists to test. All screenshots are
#      timestamped and left on disk for review afterward; this script only
#      drives the emulator, it doesn't judge the outcome itself.
#   7. Reopens pebble-another-timer afterward to capture the final state
#      (timer intact vs. reset vs. deleted).
#
# Usage:
#   ./run_conflict_test.sh [output_dir]
# output_dir defaults to ./conflict_test_<timestamp>/ under this script's
# own directory. Requires pebble-tool with an emulator already available
# (the emery platform) and no other pebble process fighting over it - the
# script does its own `pebble kill` first.
#
# TUNING (see the memory note for how these were calibrated):
#   WAKEUP_OFFSET_S   - how far out to schedule the "other app"'s wakeup.
#   REAL_DURATION_S   - the real timer's duration. For a conflict to exist
#                       at all, |REAL_DURATION_S - WAKEUP_OFFSET_S| must be
#                       small enough that the two land within Pebble's real
#                       ±60s wakeup-exclusion window of each other by the
#                       time the timer actually starts (a few seconds after
#                       scheduling, from steps 3-4 above) - too large a gap
#                       and no conflict is created at all. For the wakeup to
#                       fire WHILE the timer is still running (the
#                       interesting case) rather than after its own natural
#                       end, REAL_DURATION_S should be only a SMALL amount
#                       (10-30s) larger than WAKEUP_OFFSET_S, accounting for
#                       the few seconds of setup between scheduling and the
#                       timer actually starting - too large a buffer and
#                       tick_cb's own instant natural-fire detection wins
#                       the race every time, which is exactly what happened
#                       repeatedly when this was attempted interactively.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TIMER_APP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
TEST_APP_DIR="$SCRIPT_DIR"
TIMER_APP_UUID="1df6fc5c-261d-49c7-b339-6ea60cbe6649"
TEST_APP_UUID="89a89b3c-f84e-4233-bb93-82a9a247aab8"
EMU=emery

# 135s, calibrated from an actual timed run of this script (see the
# REAL_DURATION_S comment below for why an earlier 90s/second-precision
# attempt badly missed): installs + app-switch + opening the dial land
# around the 35-40s mark, and with whole-minute-only duration entry (a
# couple of clicks, not dozens) the timer is actually running by roughly
# 55-60s. 135s out puts the wakeup ~30-40s before a 2:00 timer's own
# natural end - comfortably inside Pebble's real ±60s exclusion window, and
# early enough that tick_cb's instant natural-fire detection (which fires
# the moment now>=end_time, foreground-only) shouldn't win the race first,
# which is what happened every time in earlier interactive testing when the
# buffer was too large.
WAKEUP_OFFSET_S=135
# Whole minutes only (a multiple of 60), deliberately: each `pebble
# emu-button` invocation costs ~2.5-3s of real CLI overhead REGARDLESS of
# who's driving it (this turned out to be the actual bottleneck, not
# per-call tool-round-trip latency as originally assumed - a first run with
# a 50-click seconds adjustment took ~140s just for that loop alone, badly
# blowing the timing budget and letting the wakeup fire mid-dial-entry
# before the timer was even created or started). Restricting to whole
# minutes means the duration dial only ever needs a handful of clicks on
# the MINUTES column (seconds stay untouched at :00), cutting entry time to
# a few seconds. 120 (2:00, a single up-click from the dial's 1:00 default).
REAL_DURATION_S=120

OUT_DIR="${1:-$SCRIPT_DIR/conflict_test_$(date +%Y%m%d_%H%M%S)}"
mkdir -p "$OUT_DIR"

START_TS=$(date +%s)
log() { printf '[%4ds] %s\n' "$(( $(date +%s) - START_TS ))" "$*"; }

shot() {
  pebble screenshot --vnc --no-open "$OUT_DIR/$1" >/dev/null 2>&1 || true
  log "screenshot: $1"
}

click() { pebble emu-button --vnc click "$1" --duration "${2:-200}" >/dev/null 2>&1; }

# Fires N clicks of one button in a tight loop - this batching (one shell
# loop instead of one interactive tool call per click) is what made duration
# entry fast enough to matter; see the memory note.
clicks() {
  local btn="$1" n="$2" dur="${3:-90}" slp="${4:-0.06}"
  for _ in $(seq 1 "$n"); do click "$btn" "$dur"; sleep "$slp"; done
}

send_msg() {
  local uuid="$1"; shift
  # "$@" (not a single quoted string) - pebble-tool's --int wants each
  # key=value as its own argv entry; passing them pre-joined as one string
  # makes it treat the whole thing as a single malformed value and exit
  # nonzero, which - under `set -e` - silently aborted the whole script
  # right after the first install.
  pebble send-app-message --emulator "$EMU" --vnc --app-uuid "$uuid" --int "$@" >/dev/null 2>&1
}

# A first `pebble install --vnc` right after a wipe/kill occasionally hits a
# libpebble2 TimeoutError while the fresh emulator instance settles (seen
# repeatedly in interactive testing too) - one `pebble kill` + retry always
# recovered there, so build that in rather than let it abort the whole run.
install_app() {
  local dir="$1" log_file
  log_file="$OUT_DIR/install_$(basename "$dir").log"
  if ! ( cd "$dir" && pebble install --emulator "$EMU" --vnc ) >"$log_file" 2>&1; then
    log "install of $(basename "$dir") failed once, retrying after kill (see $log_file)"
    pebble kill >/dev/null 2>&1 || true
    sleep 1
    ( cd "$dir" && pebble install --emulator "$EMU" --vnc ) >>"$log_file" 2>&1
  fi
}

log "=== setup ==="
pebble kill >/dev/null 2>&1 || true
pebble wipe >/dev/null 2>&1 || true

log "installing timer app"
install_app "$TIMER_APP_DIR"
# Disable idle-exit immediately - see CLAUDE.md's own warning about the
# short built-in default biting multi-step scripted walkthroughs.
send_msg "$TIMER_APP_UUID" 10001=1 10003=0 10018=0 10004=1 10011=0 10012=0 10013=1 10014=1 10015=1
log "timer app installed, idle-exit disabled"

log "installing wakeup test app"
install_app "$TEST_APP_DIR"
shot "01_test_app_idle.png"

log "=== scheduling the real wakeup (offset=${WAKEUP_OFFSET_S}s) ==="
# Whether the first ScheduleWakeup lands appears to vary run-to-run right
# after a fresh install (a run with an identical 1s pre-sleep silently lost
# the message once - status stayed "Idle" the whole way through, no
# conflict ever formed). Sending it twice is cheap insurance: do_schedule()
# is idempotent (cancels+replaces any existing reservation), so a
# double-delivery just re-arms for a couple seconds later than requested,
# not a real problem given the generous WAKEUP_OFFSET_S margin.
sleep 2
send_msg "$TEST_APP_UUID" "10000=$WAKEUP_OFFSET_S"
sleep 1
send_msg "$TEST_APP_UUID" "10000=$WAKEUP_OFFSET_S"
shot "02_test_app_armed.png"
# The test app's own auto-exit is on a 2000ms app_timer, but that's
# measured from when do_schedule() actually runs, not from when we sent
# the message - add message-processing + event-loop jitter and 2.5s cut it
# too close in practice: a first run with 2.5s here landed every subsequent
# click as a no-op on the test app's own (handler-less) idle screen, since
# it hadn't auto-exited yet - the whole rest of the run silently stayed on
# that one screen. 4s gives real margin.
sleep 4
shot "02b_after_test_app_exit.png"   # should show the watchface now - sanity check for future runs

log "=== switching to the timer app and starting a ${REAL_DURATION_S}s timer ==="
# Where exit_reason_set() + window_stack_pop_all() actually lands (plain
# watchface vs. already the launcher, with Wakeup Test pre-selected) turned
# out to vary between runs in this environment - a fixed "select, down,
# select" sequence broke when it landed on the launcher already (that
# first select just reopened Wakeup Test instead of opening the launcher).
# A few BACK presses first normalize to the watchface regardless of which
# it was (harmless/no-op once already there) before the deterministic
# launcher navigation below.
clicks back 3 200 0.3
shot "02c_normalized_to_watchface.png"   # sanity check for future runs
click select              # watchface -> launcher
sleep 0.4
click down                # Wakeup Test -> Another Timer
sleep 0.3
click select              # open the timer app
sleep 0.5
shot "03_timer_app_list.png"

click select              # "+ New timer" -> duration dial (default 00:01:00, focus=minutes)
sleep 0.4
# Minutes-only adjustment (seconds stay untouched at :00) - see the
# REAL_DURATION_S comment above for why: each click is its own ~2.5-3s
# `pebble emu-button` subprocess regardless of what drives it, so this
# needs to be as few clicks as possible, not just batched into one Bash
# call. REAL_DURATION_S must be a whole multiple of 60 for this to work.
DELTA_MIN=$(( REAL_DURATION_S / 60 - 1 ))
if [ "$DELTA_MIN" -gt 0 ]; then
  clicks up "$DELTA_MIN"
elif [ "$DELTA_MIN" -lt 0 ]; then
  clicks down "$(( -DELTA_MIN ))"
fi
shot "04_duration_set.png"
click select              # minutes -> seconds column (unchanged, still :00)
sleep 0.2
click select              # submit duration -> name entry (keyboard)
sleep 0.3
click select              # submit blank name -> timer created (+ auto-started, RunOnCreate=1)
sleep 0.6
shot "05_after_create.png"   # expect either the main list (no conflict - tune offsets) or the real conflict window

log "=== resolving the conflict with 'Keep app in foreground' ==="
click select              # row 0 is always "Keep app in foreground" - see wc_row_kind
sleep 0.4
shot "06_kept_foreground.png"

log "=== polling for the outcome (wakeup pre-emption vs. natural fire) ==="
POLL_END=$(( $(date +%s) - START_TS + WAKEUP_OFFSET_S + 60 ))
i=0
while [ "$(( $(date +%s) - START_TS ))" -lt "$POLL_END" ]; do
  i=$((i+1))
  shot "$(printf '10_poll_%02d.png' "$i")"
  sleep 5
done

log "=== reopening the timer app to capture final state (best-effort) ==="
# Assumes the last poll screenshot ended on the watchface (pre-emption
# case). If the timer instead fired naturally and its alarm is still
# unacknowledged on screen, this click lands on the alarm screen's own
# action instead of the launcher - harmless either way, just check the
# poll screenshots themselves for that case rather than these two.
click select
sleep 0.4
shot "20_final_launcher.png"
click select
sleep 0.4
shot "21_final_timer_app.png"

log "done - review screenshots under: $OUT_DIR"
