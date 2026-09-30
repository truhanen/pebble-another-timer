# Instruction dispatch: one run_* function per keyword. Sourced by
# run_sequence.sh, which must set PLATFORM, VNC, APP_UUID, RUN_OUT_DIR and
# SCREENSHOT_COUNTER (a plain variable, not exported) before calling
# dispatch_step.

# pebble_emu <subcommand> [args...]
# Runs `pebble <subcommand> --emulator $PLATFORM [--vnc] [args...]`, i.e.
# every emulator-facing keyword gets --emulator/--vnc consistently instead
# of each call site having to remember them.
pebble_emu() {
  local sub="$1"
  shift
  local flags=(--emulator "$PLATFORM")
  [ "$VNC" = "1" ] && flags+=(--vnc)
  log_cmd "pebble $sub ${flags[*]} $*"
  pebble "$sub" "${flags[@]}" "$@"
}

run_build() {
  log_cmd "pebble build"
  pebble build
}

run_install() {
  local platform="${1:-$PLATFORM}"
  local flags=(--emulator "$platform")
  [ "$VNC" = "1" ] && flags+=(--vnc)
  log_cmd "pebble install ${flags[*]}"
  pebble install "${flags[@]}"
}

# wipe and kill don't take --emulator/--vnc at all (confirmed via
# `pebble wipe --help`/`pebble kill --help`) - unlike every other
# emulator-facing subcommand, so these bypass pebble_emu deliberately.
run_wipe() {
  log_cmd "pebble wipe"
  pebble wipe
}

run_kill() {
  log_cmd "pebble kill"
  pebble kill || true
}

run_button() {
  local button="$1" action="$2"
  pebble_emu emu-button "$action" "$button"
}

# TAP/LONGPRESS use `emu-button click --duration <ms>` (a single call),
# rather than hand-timed push/sleep/release - the pebble-emulator skill
# calls this out as the documented, less-error-prone form.
run_tap() {
  local button="$1" hold_s="${2:-0.15}"
  local ms
  ms=$(awk -v s="$hold_s" 'BEGIN { printf "%d", (s * 1000) + 0.5 }')
  pebble_emu emu-button click "$button" --duration "$ms"
}

run_longpress() {
  local button="$1" hold_s="${2:-0.7}"
  run_tap "$button" "$hold_s"
}

run_sleep() {
  sleep "$1"
}

run_screenshot() {
  local label="$1"
  # Re-pin the emulator's clock (if CMD emu-set-time was ever used earlier
  # in this run - see run_cmd/_clock_pin_track below) immediately before
  # capturing. pebble-tool's own PebbleTransportPypkjs.post_connect()
  # (pebble_tool/commands/base.py) silently re-syncs the watch's clock to
  # the HOST's real current time on every new connection made through
  # pypkjs - confirmed empirically that `emu-button`/`send-app-message`
  # trigger this (screenshot/emu-set-time themselves do not), so a single
  # pin at setup time is undone by the very next TAP/APPMSG step in a real
  # sequence. Re-pinning right here, right before every screenshot, is
  # the only place a pin reliably sticks - `emu-set-time` immediately
  # followed by `screenshot` with no other command in between never
  # triggers the reset (live-verified both with and without an
  # intervening SLEEP).
  _clock_pin_repin_if_active
  SCREENSHOT_COUNTER=$((SCREENSHOT_COUNTER + 1))
  local fname
  fname=$(printf '%02d_%s.png' "$SCREENSHOT_COUNTER" "$label")
  # --no-open: without it, pebble-tool tries to open the image in a GUI
  # viewer, which hangs forever in a headless sandbox.
  pebble_emu screenshot --no-open "$RUN_OUT_DIR/$fname"
}

run_appmsg() {
  pebble_emu send-app-message --app-uuid "$APP_UUID" "$@"
}

# Shared preconditions for TOUCH/TOUCHDOWN/TOUCHUP - see run_touch's own
# comment below for the full "why" behind each check.
_touch_preflight() {
  if [ "$VNC" = "1" ]; then
    log_error "$1 requires a real windowed emulator (--no-vnc) - qemu's own --vnc framebuffer never delivers touch input to the guest. Run this sequence via tests/functional/docker/run_container.sh --touch (see its README's TOUCH section)."
    return 1
  fi
  if [ -z "${DISPLAY:-}" ]; then
    log_error "$1 requires \$DISPLAY (an Xvfb-backed X11 display for the emulator's real window) - not supported outside tests/functional/docker/run_container.sh --touch."
    return 1
  fi
  if ! command -v xdotool >/dev/null 2>&1; then
    log_error "$1 requires xdotool, not found on PATH - container-only feature, see tests/functional/docker/Containerfile."
    return 1
  fi
}

# Finds the emulator's real window and gives it X input focus (no window
# manager in this container's Xvfb, so focus is never assigned
# automatically - without this, a click reaches the window but never
# registers as touch, live-verified). Echoes the window id, or returns
# non-zero with nothing echoed if not found.
_touch_find_window() {
  local win
  win="$(xdotool search --name QEMU 2>/dev/null | head -1)"
  if [ -z "$win" ]; then
    log_error "$1: no QEMU window found via 'xdotool search --name QEMU' - is the emulator installed and running?"
    return 1
  fi
  xdotool windowfocus --sync "$win" 2>/dev/null || true
  echo "$win"
}

# TOUCH x y [hold_s] - a real touchscreen tap-and-hold at content-relative
# coordinates (x y), via xdotool driving the emulator's own real SDL/X11
# window - NOT through pebble-tool/qemu's own --vnc framebuffer, which is
# display-only and never delivers touch input to the guest at all (live-
# verified: a debug APP_LOG in touch_dial/touch.c's handle_touch_event
# never fired for a raw VNC pointer click, but did fire, at the exact
# target coordinates, for an xdotool click against a real windowed
# emulator). This is why every emulator-facing keyword above unconditionally
# honors $VNC while this one requires it to be OFF - see the container
# entrypoint's own comment (tests/functional/docker/run-sequence-in-
# container.sh) for how a sequence opts into that.
#
# hold_s (default 0.6s) is a genuine HOLD, not a tap: this app treats a
# too-quick tap-and-release as a cancelled gesture and reverts out of
# touch-select mode without ever visibly opening the touch dial
# (touch_dial/config.c's touchLiftMinDurationMs, default 100ms) - 0.6s
# comfortably clears that with margin to spare, matching what was live-
# verified (via the same debug-log method above, and visually - a
# screenshot taken mid-hold, before liftoff, showing the actual round dial
# UI - the dial closes again immediately on liftoff, so a screenshot taken
# AFTER releasing never shows it) to actually open the dial.
#
# This atomic form (down+hold+up in one call) is right for a single
# committed/cancelled gesture, but can never itself produce a screenshot of
# the dial actually OPEN, since it always releases before returning - use
# TOUCHDOWN/TOUCHUP (below) instead, with a SCREENSHOT step in between, to
# capture that.
#
# Container-only by design - see Containerfile's own comment for why there
# is no supported native equivalent (delivering a correctly hit-tested
# click into a specific window requires moving the real OS cursor, and the
# one tested way to avoid that - CGEventPostToPid on macOS - does not
# properly window-hit-test and delivers no touch event at all).
run_touch() {
  local x="$1" y="$2" hold_s="${3:-0.6}"
  _touch_preflight TOUCH || return 1
  local win
  win="$(_touch_find_window TOUCH)" || return 1
  log_cmd "xdotool mousemove --window $win $x $y mousedown 1 sleep $hold_s mouseup 1"
  xdotool mousemove --window "$win" "$x" "$y" mousedown 1 sleep "$hold_s" mouseup 1
}

# TOUCHDOWN x y - presses and HOLDS at content-relative coordinates (x y),
# leaving the touch active until a later TOUCHUP - the split form of TOUCH,
# for a sequence that wants to SCREENSHOT the dial while it's actually
# open (see run_touch's own comment for why the atomic TOUCH can't do
# this: it always releases before returning). Same preconditions/mechanism
# as TOUCH - see its comment for the full "why".
run_touchdown() {
  local x="$1" y="$2"
  _touch_preflight TOUCHDOWN || return 1
  local win
  win="$(_touch_find_window TOUCHDOWN)" || return 1
  log_cmd "xdotool mousemove --window $win $x $y mousedown 1"
  xdotool mousemove --window "$win" "$x" "$y" mousedown 1
}

# TOUCHUP - releases a touch previously started by TOUCHDOWN, at wherever
# the (virtual) pointer currently is - no coordinates needed, matching
# TOUCHDOWN's own position. Takes no arguments; a TOUCHUP with nothing
# currently pressed is a no-op as far as xdotool is concerned (there is no
# separate "was anything down" state to check here).
run_touchup() {
  _touch_preflight TOUCHUP || return 1
  log_cmd "xdotool mouseup 1"
  xdotool mouseup 1
}

# TOUCHMOVE x y - moves an ALREADY-DOWN touch (from TOUCHDOWN) to new
# content-relative coordinates, generating a single TouchEvent_PositionUpdate
# - the building block for a custom multi-point drag. Most sequences want
# TOUCHDRAG (below) instead, which handles the common case (a circular arc)
# directly; reach for this only for a path TOUCHDRAG doesn't cover. Does not
# re-focus the window (assumes a just-preceding TOUCHDOWN already did) - see
# TOUCH's own comment for the full "why" behind the preconditions.
run_touchmove() {
  local x="$1" y="$2"
  _touch_preflight TOUCHMOVE || return 1
  local win
  win="$(_touch_find_window TOUCHMOVE)" || return 1
  log_cmd "xdotool mousemove --window $win $x $y"
  xdotool mousemove --window "$win" "$x" "$y"
}

# Shared by TOUCHDRAG/TOUCHSWEEP - see TOUCHDRAG's own comment for the
# angle convention and why an arc (not a straight chord) is needed. Prints
# one "x y" waypoint per line, from start_deg to end_deg inclusive.
_touch_arc_waypoints() {
  local cx="$1" cy="$2" radius="$3" start_deg="$4" end_deg="$5" steps="$6"
  awk -v cx="$cx" -v cy="$cy" -v r="$radius" \
      -v a0="$start_deg" -v a1="$end_deg" -v n="$steps" '
    BEGIN {
      pi = atan2(0, -1)
      for (i = 0; i <= n; i++) {
        deg = a0 + (a1 - a0) * i / n
        rad = deg * pi / 180
        x = cx + r * sin(rad)
        y = cy - r * cos(rad)
        printf "%d %d\n", (x >= 0 ? int(x + 0.5) : int(x - 0.5)), (y >= 0 ? int(y + 0.5) : int(y - 0.5))
      }
    }'
}

# TOUCHSWEEP cx cy radius start_deg end_deg [steps] - moves an ALREADY-DOWN
# touch (from TOUCHDOWN) along a circular arc, WITHOUT releasing at the end
# - the split form of TOUCHDRAG (below), for a sequence that wants to
# SCREENSHOT the dial mid-drag (e.g. already in a different submode, like
# seconds-windup, but before liftoff commits it - see TOUCH's own comment
# for why an atomic gesture can't itself produce that screenshot). Pair
# with a TOUCHDOWN at the same start_deg point beforehand and a TOUCHUP
# afterward. See TOUCHDRAG for the full angle-convention/arc-vs-chord
# rationale and the steps/crossing-detection guidance - identical here.
run_touchsweep() {
  local cx="$1" cy="$2" radius="$3" start_deg="$4" end_deg="$5" steps="${6:-24}"
  _touch_preflight TOUCHSWEEP || return 1
  local win
  win="$(_touch_find_window TOUCHSWEEP)" || return 1
  log_cmd "xdotool mousemove --window $win <arc waypoints> ($steps-step sweep $start_deg -> $end_deg deg)"
  local wx wy
  while IFS=' ' read -r wx wy; do
    xdotool mousemove --window "$win" "$wx" "$wy"
    sleep 0.03
  done <<< "$(_touch_arc_waypoints "$cx" "$cy" "$radius" "$start_deg" "$end_deg" "$steps")"
}

# TOUCHDRAG cx cy radius start_deg end_deg [steps] [hold_s] - a circular-arc
# drag: this app's touch dial reads only the ANGLE from centre (via atan2 -
# see touch_dial/touch.c's angle_between_points/selected_segment), so a
# realistic drag between two non-adjacent angles must move along an ARC at
# constant radius, not a straight chord - a straight line between two points
# on opposite sides would cut through the centre (the inner "Cancel" zone)
# partway through, corrupting the gesture (dropping into TOUCH_AREA_INNER
# resets the field currently being picked - see handle_touch_event).
#
# Degrees are this widget's own screenspace convention: 0 = straight up,
# increasing CLOCKWISE (see touch_dial/misc.c's point_from_angle, whose
# exact formula - x = cx + r*sin(deg), y = cy - r*cos(deg) - this
# reproduces). end_deg may be negative or exceed 360 to express continued
# rotation past the 0/360 boundary - e.g. start_deg=150 end_deg=-30 sweeps
# 180 degrees anticlockwise, continuing 30 degrees past the "12 o'clock"/
# 0-minute mark, which is exactly this app's seconds-windup gesture (drag
# anticlockwise past both the 20-minute mark and 0 to set a sub-minute
# duration - see touch_dial/touch.c's apply_windup, and
# touch_dial_below_one_minute.seq for a worked, live-verified example).
#
# steps (default 24) is how many waypoints to interpolate between start_deg
# and end_deg - keep each step's angular delta well inside apply_windup's
# crossing-detection windows (~45 degrees wide) so a crossing can't be
# skipped over; 24 steps across a 180-degree sweep is 7.5 degrees/step,
# comfortably under that. hold_s (default 0.6s) is applied at the FINAL
# waypoint before release - see TOUCH's own comment for why a genuine hold
# (not a bare release) matters.
#
# Atomic (down, full sweep, hold, release, all in one call) - can't itself
# produce a screenshot of a mid-drag state; use TOUCHDOWN/TOUCHSWEEP/TOUCHUP
# instead for that (see touch_dial_below_one_minute.seq for a worked
# example capturing the dial already in seconds-windup mode, before the
# final release commits it).
run_touchdrag() {
  local cx="$1" cy="$2" radius="$3" start_deg="$4" end_deg="$5"
  local steps="${6:-24}" hold_s="${7:-0.6}"
  _touch_preflight TOUCHDRAG || return 1
  local win
  win="$(_touch_find_window TOUCHDRAG)" || return 1

  xdotool windowfocus --sync "$win" 2>/dev/null || true

  local first=1 wx wy
  while IFS=' ' read -r wx wy; do
    if [ "$first" = "1" ]; then
      log_cmd "xdotool mousemove --window $win $wx $wy mousedown 1 (arc start, $steps-step sweep $start_deg -> $end_deg deg)"
      xdotool mousemove --window "$win" "$wx" "$wy" mousedown 1
      first=0
    else
      xdotool mousemove --window "$win" "$wx" "$wy"
      sleep 0.03
    fi
  done <<< "$(_touch_arc_waypoints "$cx" "$cy" "$radius" "$start_deg" "$end_deg" "$steps")"

  log_cmd "xdotool sleep $hold_s mouseup 1 (arc end)"
  sleep "$hold_s"
  xdotool mouseup 1
}

# Clock-pin tracking for run_screenshot's re-pin above. Set by run_cmd
# whenever a sequence issues `CMD emu-set-time <HH:MM:SS|epoch>` (e.g.
# wipe_and_prep.seq's default pin) - CLOCK_PIN_TS is the pinned moment as
# a Unix epoch, CLOCK_PIN_HOST_TS is the *host's* wall-clock epoch at the
# moment it was pinned, so later re-pins can compute "how much simulated
# time should have elapsed since the pin" from real elapsed host time
# (the emulator's clock otherwise ticks forward at normal real-time rate,
# so this stays accurate) rather than freezing the clock at one value
# forever.
CLOCK_PIN_TS=""
CLOCK_PIN_HOST_TS=""

_clock_pin_parse_epoch() {
  local time_arg="$1"
  if [[ "$time_arg" =~ ^[0-9]+$ ]]; then
    echo "$time_arg"
    return 0
  fi
  # BSD date (macOS) first, then GNU date (Linux) - see run_cmd's own note
  # on why HH:MM:SS needs converting to an epoch here at all.
  date -j -f "%H:%M:%S" "$time_arg" +%s 2>/dev/null \
    || date -d "$time_arg" +%s 2>/dev/null
}

_clock_pin_repin_if_active() {
  [ -n "$CLOCK_PIN_TS" ] || return 0
  local now_host target
  now_host=$(date +%s)
  target=$((CLOCK_PIN_TS + (now_host - CLOCK_PIN_HOST_TS)))
  pebble_emu emu-set-time "$target"
}

run_cmd() {
  local sub="$1"
  shift
  if [ "$sub" = "emu-set-time" ]; then
    # Track the pin so run_screenshot can keep re-establishing it later -
    # converted to an epoch immediately (rather than re-parsing "HH:MM:SS"
    # relative to "today" again on every later re-pin, which would silently
    # roll the date forward incorrectly on any run crossing midnight).
    local epoch
    epoch=$(_clock_pin_parse_epoch "$1")
    if [ -n "$epoch" ]; then
      CLOCK_PIN_TS="$epoch"
      CLOCK_PIN_HOST_TS=$(date +%s)
    fi
  fi
  pebble_emu "$sub" "$@"
}

run_var() {
  local assignment="$1"
  eval "${assignment%%=*}=\"${assignment#*=}\""
}

# dispatch_step <origin> <instruction>
# Expands $VAR/${VAR} references and shell-quoting in <instruction>, then
# calls the matching run_* function.
dispatch_step() {
  local origin="$1" instruction="$2"
  local args=()

  if ! eval "args=($instruction)" 2>/dev/null; then
    log_error "$origin: failed to parse instruction: $instruction"
    return 1
  fi

  local kw="${args[0]}"
  local rest=("${args[@]:1}")

  case "$kw" in
    BUILD) run_build ;;
    INSTALL) run_install "${rest[@]+"${rest[@]}"}" ;;
    WIPE) run_wipe ;;
    KILL) run_kill ;;
    BUTTON) run_button "${rest[@]+"${rest[@]}"}" ;;
    TAP) run_tap "${rest[@]+"${rest[@]}"}" ;;
    LONGPRESS) run_longpress "${rest[@]+"${rest[@]}"}" ;;
    SLEEP) run_sleep "${rest[@]+"${rest[@]}"}" ;;
    SCREENSHOT) run_screenshot "${rest[@]+"${rest[@]}"}" ;;
    APPMSG) run_appmsg "${rest[@]+"${rest[@]}"}" ;;
    TOUCH) run_touch "${rest[@]+"${rest[@]}"}" ;;
    TOUCHDOWN) run_touchdown "${rest[@]+"${rest[@]}"}" ;;
    TOUCHUP) run_touchup ;;
    TOUCHMOVE) run_touchmove "${rest[@]+"${rest[@]}"}" ;;
    TOUCHSWEEP) run_touchsweep "${rest[@]+"${rest[@]}"}" ;;
    TOUCHDRAG) run_touchdrag "${rest[@]+"${rest[@]}"}" ;;
    CMD) run_cmd "${rest[@]+"${rest[@]}"}" ;;
    LOG) log_info "${rest[*]}" ;;
    VAR) run_var "${rest[@]+"${rest[@]}"}" ;;
    "") log_error "$origin: empty instruction" ; return 1 ;;
    *) log_error "$origin: unknown instruction keyword '$kw'"; return 1 ;;
  esac
}
