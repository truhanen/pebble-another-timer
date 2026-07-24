#!/usr/bin/env bash
# Captures reference screenshots of the app's main views using the Pebble CLI
# emulator.
#
# Two views require you at the keyboard/mouse, because the Pebble CLI has no
# way to synthesize touch events on the emulator:
#   - the touch dial (only drawn while a finger is held on the screen)
#   - the label keyboard (a touch multitap layout)
# The script pauses at those points and waits for you to press Enter, so run
# it in an interactive terminal.
#
# NOTE: this kills ALL currently running Pebble emulators (any platform) and
# wipes their on-watch storage, so it can start each phase from a known-empty
# timer list. Don't run it if you have unsaved state in another emulator.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="emery"
OUT_DIR="$REPO_ROOT/screenshots"
SKIP_BUILD=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [-p PLATFORM] [-o OUT_DIR] [--skip-build]

  -p, --platform   Emulator platform: basalt|diorite|emery|flint. Default: emery.
  -o, --output     Directory to write screenshots into. Default: $OUT_DIR
      --skip-build Skip "pebble build" and reuse the existing build/ output.
  -h, --help       Show this help.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -p|--platform) PLATFORM="$2"; shift 2 ;;
    -o|--output) OUT_DIR="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

cd "$REPO_ROOT"
mkdir -p "$OUT_DIR"

PBW="build/PebbleCountdownTimer.pbw"

log() { printf '\n=== %s ===\n' "$1"; }

pause_for_manual_step() {
  printf '\n%s\n' "$1"
  read -r -p "Press Enter when ready... " _
}

# Prompts, waits for Enter (while you're still free to read/type here), then
# counts down out loud so you can switch to the emulator and get into
# position (e.g. touching and holding the dial) before the screenshot fires -
# no need to switch back to the terminal while still touching the screen.
countdown_then_shoot() {
  # $1: instructions   $2: output filename   $3: countdown seconds (default 4)
  local secs="${3:-4}"
  printf '\n%s\n' "$1"
  read -r -p "Press Enter to start a ${secs}s countdown, then switch to the emulator... " _
  local n="$secs"
  while [ "$n" -gt 0 ]; do
    printf '%d... ' "$n"
    sleep 1
    n=$((n - 1))
  done
  echo "capturing now"
  shoot "$2"
}

shoot() {
  # $1: output filename (without directory)
  pebble screenshot --emulator "$PLATFORM" --no-open "$OUT_DIR/$1"
  echo "Saved $OUT_DIR/$1"
}

button() {
  # $1: back|up|select|down   $2: seconds to sleep afterwards (default 1)
  pebble emu-button --emulator "$PLATFORM" click "$1"
  sleep "${2:-1}"
}

long_press_select() {
  # $1: seconds to sleep afterwards (default 1). A plain `button select` is a
  # quick click; the per-row edit menu (Duration/Label/After
  # finished) only opens on a genuine long-press, which `pebble emu-button`
  # can't synthesize directly - hold (push, wait, release) instead, same as
  # `make long_press_select_emulator`.
  pebble emu-button --emulator "$PLATFORM" push select
  sleep 0.3
  pebble emu-button --emulator "$PLATFORM" release select
  sleep "${1:-1}"
}

install_fresh() {
  # Kill any running emulators and wipe their storage first: `pebble wipe`
  # only deletes the on-disk persist directory, which a live qemu process
  # won't reload on its own, so the kill is required for a truly empty list.
  log "Resetting the emulator (kill + wipe) and installing"
  pebble kill
  pebble wipe
  pebble install --emulator "$PLATFORM" "$PBW"
  sleep 3
  # Two config values fight this script if left at their defaults:
  #  - IdleExitSec defaults to 15 (timer_store.c) - a manual pause here
  #    (typing a label, getting into position for a touch screenshot) can
  #    easily run longer than that and get the app kicked back to the
  #    watchface mid-flow.
  #  - AutoReturnStart/AutoReturnStop default to true/ON (timer_store.c) -
  #    starting or pausing/stopping a timer would immediately flash-and-pop
  #    back to the watchface, exiting the app before the next
  #    screenshot/button step.
  #  - RunOnCreate defaults to true/ON (timer_store.c) - the "+ New timer"
  #    dial+label flow in phase 1 would immediately start the timer it
  #    creates, but that phase reuses the just-created timer for the
  #    "one unstarted timer" screenshot, which needs it left idle.
  # Turn all off, and send them twice: on launch the watch also asks the
  # phone (pypkjs) for its last-saved config (request_config() in main.c),
  # and pypkjs's own storage isn't necessarily cleared by `pebble wipe`
  # (that only clears the watch-side persist dir), so a stale saved config
  # can echo the old values back and race with our disable message.
  # Resending a moment later guarantees ours is the last word.
  log "Disabling idle auto-exit, start/stop auto-return, and run-on-create"
  pebble send-app-message --emulator "$PLATFORM" \
    --int "${IDLE_EXIT_KEY}=0" "${AUTO_RETURN_START_KEY}=0" "${AUTO_RETURN_STOP_KEY}=0" "${RUN_ON_CREATE_KEY}=0"
  sleep 1.5
  pebble send-app-message --emulator "$PLATFORM" \
    --int "${IDLE_EXIT_KEY}=0" "${AUTO_RETURN_START_KEY}=0" "${AUTO_RETURN_STOP_KEY}=0" "${RUN_ON_CREATE_KEY}=0"
  sleep 1
}

if [ "$SKIP_BUILD" -eq 0 ]; then
  log "Building app"
  pebble build
fi

if [ ! -f "$PBW" ]; then
  echo "Could not find $PBW - build failed?" >&2
  exit 1
fi

# Read numeric AppMessage keys from the just-built output instead of
# hardcoding them: they're assigned by build order from package.json's
# messageKeys list and could change if that list is edited.
message_key() {
  # $1: key name, e.g. TimerConfig. Lines look like
  # "uint32_t MESSAGE_KEY_TimerConfig = 10000;" - grab the value after '=',
  # not just any digit run (that would also match the "32" in "uint32_t").
  sed -n "s/^uint32_t MESSAGE_KEY_$1 = \([0-9]*\);/\1/p" build/src/message_keys.auto.c
}
TIMER_CONFIG_KEY="$(message_key TimerConfig)"
IDLE_EXIT_KEY="$(message_key IdleExitSec)"
AUTO_RETURN_START_KEY="$(message_key AutoReturnStart)"
AUTO_RETURN_STOP_KEY="$(message_key AutoReturnStop)"
RUN_ON_CREATE_KEY="$(message_key RunOnCreate)"
SET_TIMER_INDEX_KEY="$(message_key SetTimerIndex)"
SET_TIMER_STATE_KEY="$(message_key SetTimerState)"
SET_TIMER_REMAINING_KEY="$(message_key SetTimerRemaining)"
if [ -z "$TIMER_CONFIG_KEY" ] || [ -z "$IDLE_EXIT_KEY" ] || [ -z "$AUTO_RETURN_START_KEY" ] \
    || [ -z "$AUTO_RETURN_STOP_KEY" ] || [ -z "$RUN_ON_CREATE_KEY" ] || [ -z "$SET_TIMER_INDEX_KEY" ] \
    || [ -z "$SET_TIMER_STATE_KEY" ] || [ -z "$SET_TIMER_REMAINING_KEY" ]; then
  echo "Could not resolve message keys from build/src/message_keys.auto.c" >&2
  exit 1
fi

# Force the timer at list index $1 into state $2 (0=idle/stopped, 1=running,
# 2=paused - matches TimerState in src/c/timer_calc.h) with $3 seconds
# remaining. Bypasses the start/pause/reset button flow entirely (see
# SetTimerIndex/State/Remaining handling in src/c/main.c's inbox_received) -
# a data-only change, so it doesn't open/close any window.
set_timer() {
  pebble send-app-message --emulator "$PLATFORM" \
    --int "${SET_TIMER_INDEX_KEY}=$1" "${SET_TIMER_STATE_KEY}=$2" "${SET_TIMER_REMAINING_KEY}=$3"
}

# ---------------------------------------------------------------------------
# Phase 1: duration dial, duration touch dial, and label input - all reached
# via the "+ New timer" flow on a freshly emptied timer list - plus the main
# view with a single unstarted timer, reusing the timer this flow just
# created instead of a separate install+config round trip (RunOnCreate is
# disabled above, so committing the label leaves it idle rather than
# starting it).
# ---------------------------------------------------------------------------
install_fresh

log "Main view (no timers)"
shoot "07_main_view_no_timers.png"

log "Duration dial view"
button select 2   # "+ New timer" (the only row) -> opens the box-style duration dial
shoot "02_duration_dial.png"

countdown_then_shoot "Next: touch and HOLD the dial face on the emulator
window to reveal the touch dial." "03_duration_touch_dial.png" 4
pause_for_manual_step "Release the touch now to submit the duration - the
touch dial confirms in one shot on liftoff and jumps straight to the label
keyboard (no button presses needed), unlike the physical-button dial. Press
Enter here once the label keyboard is showing."

log "Label input view"
pause_for_manual_step "The label keyboard is open. Type a label using the
on-screen touch keys (e.g. \"Pasta\"), then press Enter here to capture it."
shoot "04_label_input.png"
button select 1   # submit the typed label -> commits the new timer

log "Main view (one timer, '+ New timer' selected)"
button down 1   # commit left the cursor on the just-created timer's row (the
                # only row) - move down onto the trailing "+ New timer" row
shoot "08_main_view_one_timer.png"

# ---------------------------------------------------------------------------
# Phase 2: main list (one running, one paused, two stopped, all labeled) and
# the running timer's control menu - all state configured via send-app-message,
# no touch/typing, and (thanks to SetTimerIndex/State/Remaining) no button
# presses either for the start/pause transitions themselves.
# ---------------------------------------------------------------------------
install_fresh

log "Configuring 4 labeled timers via send-app-message"
US=$'\x1f'   # field separator between a timer's name and its duration
RS=$'\x1e'   # record separator between timers
TIMER_CONFIG="Pasta${US}600${RS}Tea${US}180${RS}Laundry${US}1500${RS}Workout${US}3600"
pebble send-app-message --emulator "$PLATFORM" --string "${TIMER_CONFIG_KEY}=${TIMER_CONFIG}"
sleep 2

log "Starting Pasta (index 0) and Tea (index 1) via send-app-message"
set_timer 0 1 510   # Pasta: running, 8:30 left
set_timer 1 1 75    # Tea: running, 1:15 left
sleep 1

# The list was empty when the app launched, so the selection was sitting on
# the trailing "+ New timer" row; after the config lands it's still on that
# row, now pushed past the 4 new timers. Walk up to Tea (2nd row) to open its
# control menu below - Pasta and Tea are already running, set above.
button up 0.5
button up 0.5
button up 1

log "Running timer control menu"
button select 1   # Tea is running -> SELECT opens its control menu
shoot "05_running_timer_control_menu.png"

log "Closing control menu"
button back 1

log "Timer edit menu (long press)"
long_press_select 1   # long-press Tea's row -> per-row edit menu (Duration/label, After finished)
shoot "06_timer_edit_menu.png"

log "Closing timer edit menu"
button back 1

log "Pausing Tea (index 1) via send-app-message"
set_timer 1 2 75
sleep 1

log "Main view (1 running, 1 paused, 2 stopped, all labeled)"
shoot "01_main_view.png"

log "Done - screenshots saved in $OUT_DIR"
