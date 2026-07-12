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
  #  - AutoReturn defaults to true/ON (timer_store.c) - starting or
  #    pausing a timer would immediately flash-and-pop back to the
  #    watchface, exiting the app before the next screenshot/button step.
  # Turn both off, and send them twice: on launch the watch also asks the
  # phone (pypkjs) for its last-saved config (request_config() in main.c),
  # and pypkjs's own storage isn't necessarily cleared by `pebble wipe`
  # (that only clears the watch-side persist dir), so a stale saved config
  # can echo the old values back and race with our disable message.
  # Resending a moment later guarantees ours is the last word.
  log "Disabling idle auto-exit and start/stop auto-return"
  pebble send-app-message --emulator "$PLATFORM" \
    --int "${IDLE_EXIT_KEY}=0" "${AUTO_RETURN_KEY}=0"
  sleep 1.5
  pebble send-app-message --emulator "$PLATFORM" \
    --int "${IDLE_EXIT_KEY}=0" "${AUTO_RETURN_KEY}=0"
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
AUTO_RETURN_KEY="$(message_key AutoReturn)"
if [ -z "$TIMER_CONFIG_KEY" ] || [ -z "$IDLE_EXIT_KEY" ] || [ -z "$AUTO_RETURN_KEY" ]; then
  echo "Could not resolve message keys from build/src/message_keys.auto.c" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Phase 1: duration dial, duration touch dial, and label input - all reached
# via the "+ New timer" flow on a freshly emptied timer list.
# ---------------------------------------------------------------------------
install_fresh

log "Duration dial view"
button select 2   # "+ New timer" (the only row) -> opens the box-style duration dial
shoot "02_duration_dial.png"

countdown_then_shoot "Next: touch and HOLD the dial face on the emulator
window to reveal the touch dial." "03_duration_touch_dial.png" 4
pause_for_manual_step "Release the touch now, then press Enter to continue."

log "Label input view"
button select 1   # minutes field -> seconds field
button select 2   # confirm duration -> opens the (touch) label keyboard
pause_for_manual_step "The label keyboard is open. Type a label using the
on-screen touch keys (e.g. \"Pasta\"), then press Enter here to capture it."
shoot "04_label_input.png"

# ---------------------------------------------------------------------------
# Phase 2: main list (one running, one paused, two stopped, all labeled) and
# the running timer's control menu - configured via send-app-message so no
# touch/typing is needed.
# ---------------------------------------------------------------------------
install_fresh

log "Configuring 4 labeled timers via send-app-message"
US=$'\x1f'   # field separator between a timer's name and its duration
RS=$'\x1e'   # record separator between timers
TIMER_CONFIG="Pasta${US}600${RS}Tea${US}180${RS}Laundry${US}1500${RS}Workout${US}3600"
pebble send-app-message --emulator "$PLATFORM" --string "${TIMER_CONFIG_KEY}=${TIMER_CONFIG}"
sleep 2

# The list was empty when the app launched, so the selection was sitting on
# the trailing "+ New timer" row; after the config lands it's still on that
# row, now pushed past the 4 new timers. Walk back up to the first one.
button up 0.5
button up 0.5
button up 0.5
button up 1

log "Starting the first timer (Pasta -> running)"
button select 1

log "Starting the second timer (Tea -> running)"
button down 1
button select 1

log "Running timer control menu"
button select 1   # Tea is now running -> SELECT opens its control menu
shoot "05_running_timer_control_menu.png"

log "Pausing the second timer (Tea -> paused)"
button down 1     # highlight "Pause"
button select 2   # confirm -> back to the main list

log "Main view (1 running, 1 paused, 2 stopped, all labeled)"
shoot "01_main_view.png"

log "Done - screenshots saved in $OUT_DIR"
