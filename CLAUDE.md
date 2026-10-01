# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Pebble watchapp (C + Pebble SDK, targets emery) with a
multi-timer list on the watch and a Clay-based config page on the phone.
Phone-side logic is written in TypeScript, compiled to PebbleKit JS.

## Build & test commands

Before installing, driving, or debugging the app in the emulator (any
`pebble install --emulator`/`pebble screenshot`/`pebble emu-button`/`pebble
send-app-message`/`pebble wipe`/`pebble logs` session), use the
`pebble-emulator` skill — it covers headless/agentic reliability gotchas
(`--vnc`, idle auto-exit, detecting the selected row from a screenshot,
simulating long-presses) that the rest of this section assumes.

```bash
npm install
pebble build                 # runs tsc (src/ts -> src/pkjs) via wscript hook, then bundles
pebble install --emulator emery

npm run typecheck            # tsc --noEmit
npm test                     # node --test tests/*.test.js (runs pretest: tsc first)

# pure C core, no Pebble SDK needed:
gcc -I src/c tests/test_timer_calc.c src/c/timer_calc.c -o /tmp/t && /tmp/t
```

`src/pkjs/*.js` is **generated and gitignored** — always edit `src/ts/*.ts`,
never `src/pkjs/`. `pebble build` regenerates it via `tsc` (config in
`tsconfig.json`, target ES5/CommonJS) before the SDK bundles it; a type error
aborts the build (`noEmitOnError`).

`pebble build` (via `waf`) routinely exceeds a 2-3 minute foreground command
timeout in an agent sandbox, especially on the first build after a clean/
`npm install` (arm-none-eabi toolchain setup, full recompile). Don't treat
that timeout as a failure and blindly re-run the command — if it gets moved
to a background task, wait for that task's own completion notification and
read its actual output (exit code + "Compiling emery"/"Linking emery"/
"'build' finished successfully" lines) before concluding the build passed
or failed. Re-running `pebble build` while a prior invocation is still
running in the background stacks a second concurrent `waf`/toolchain
process against the same `build/` output directory, which can race on
generated files - confirm the prior run has actually finished (check for
completion, or `ps aux | grep -i waf` if unsure) before starting another.
Same goes for `pebble install --emulator`/other long-running `pebble`
invocations: let a backgrounded one finish (or explicitly `pebble kill` it)
rather than layering a fresh one on top.

Other Makefile targets: `make clean`, `make kill_emulator`, `make wipe_emulator`,
`make wipe_and_prep_emulator` (see below), `make build_and_install_emulator`,
`make install_cloudpebble`.

Two targets simulate the phone side without a real Clay/phone app attached to
the emulator — useful since PebbleKit JS/AppMessage doesn't run against the
emulator on its own:
- `make send_emulator_configuration` — pushes a fixed settings config
  (SortOrder/AutoReturn/RunningFirst/IdleExitSec/CfgOpen/LaunchSync) via
  `pebble send-app-message --int`, using message-key IDs and values defined in
  `emulator_configuration.mk` (keys follow `package.json`'s `messageKeys`
  numbering, `10000 + index`). Edit that file's `_VAL`s to test different
  settings combinations.
- `make send_emulator_timers` — pushes a canned `TimerConfig` string (key
  `10000`) with three demo timers, via `--string`, to exercise the watch-side
  list without going through the phone config flow at all.
- `make long_press_select_emulator` — simulates a long SELECT press
  (`pebble emu-button push` + sleep 0.3 + `release`) to open the per-row
  detail menu, since a real long-press is hard to trigger through
  `pebble emu-button` otherwise.

When navigating the main timer list by screenshot, **the selected row has a
solid black background (white text)** — this is the only reliable selection
indicator (see the `pebble-emulator` skill for the general pixel-sampling
technique for when this is ambiguous at native screenshot resolution). Bold
vs. non-bold text is unrelated to selection (it's tied to timer
state/other row styling) and has caused wrongly-selected-row test failures
before; don't infer selection from it. If no row visibly has a black
background, selection is very likely on the "+ New timer" row — SELECT
there jumps straight into a blank new-timer duration dial (header "Duration",
defaulting to `00:01:00`), which is a common way to end up somewhere
unexpected in a scripted walkthrough.

Before manually driving the emulator through any multi-step flow (navigating
menus, opening the detail screen, toggling settings, ...), disable idle
auto-exit first: run `make send_emulator_configuration` with
`EMULATOR_CFG_IDLEEXIT_VAL` set to `0` in `emulator_configuration.mk` (the
checked-in default), and push it once the app is already open (idle-exit
config only takes effect once the app has received it — a fresh/wiped
watch's built-in default idle timeout is short; see the skill for why this
matters for a scripted, multi-command walkthrough). `pebble wipe` (see below)
resets this along with everything else, so re-push the idle-exit-disabled
configuration again every time after wiping, before starting the next manual
walkthrough.

Both `send_emulator_*` targets hardcode `--app-uuid` and the `emery`
platform (see the skill for why a mismatched UUID matters); if
`package.json`'s `pebble.uuid` (`1df6fc5c-261d-49c7-b339-6ea60cbe6649`) ever
changes, update these targets in `Makefile` to match.

Build-time env flags (see `wscript`): `FAKE_TIME=1` defines `USE_FAKE_TIME`;
`SCREENSHOT_FIXTURES=1` defines `SCREENSHOT_FIXTURES` to seed demo data for
appstore screenshots (see `scripts/`).

Use `--vnc` on every emulator-facing command (`install`, `screenshot`,
`ping`, `emu-button`, `send-app-message`, `logs`) in this project — this
matches pebble-tool's own recommendation for headless sessions, and a
dedicated test round in this project's agent sandbox confirmed 20+
consecutive `--vnc` calls all succeeding immediately with zero timeouts.
Once an emulator instance is started with `--vnc`, keep it consistent for
every subsequent command against that same instance — see the skill for
the don't-mix-`--vnc`-modes rule and for how to recover if commands do
start timing out (kill/wipe/reinstall, not dropping `--vnc`).

**Always use `make wipe_and_prep_emulator` instead of bare `pebble wipe`.**
`pebble wipe` resets the watch to firmware defaults, which includes a short
built-in idle-auto-exit timeout — and since a manual test walkthrough here is
a sequence of separate `pebble` CLI calls (each with multiple seconds of
overhead), it's very easy to get silently idle-exited mid-walkthrough right
after a wipe, before you've had a chance to push the idle-exit-disabled
config. `wipe_and_prep_emulator` (in `Makefile`) wipes, reinstalls, opens the
app, and pushes `send_emulator_configuration` (idle exit off) in one step, so
the emulator is immediately safe to drive by hand afterward.

## Architecture

### Watch side (`src/c/`)

- **`timer_calc.c`/`.h`** — the pure, host-testable core. Owns the `Timer`
  struct, `TimerState`/`SortMode`/`DetailAction` enums, config-string
  parsing (`tc_parse_config`, the RS/US-delimited format shared with
  `src/ts/timer_config.ts`), time formatting, state transitions
  (`tc_start`/`pause`/`reset`/`extend`/`add`), expiry checks, sort/display
  ordering, and `tc_reconcile` (merges an incoming phone config over live
  watch state while preserving already-running timers). No Pebble SDK
  dependency — this is what `tests/test_timer_calc.c` links directly.
- **`timer_store.c`/`.h`** — persistence only: marshals timers/settings to
  Pebble persistent storage (one key per timer at `PERSIST_KEY_TIMER_BASE +
  i`, plus scalar keys for schema version, count, wakeup id, sort mode,
  auto-return/running-first/idle-exit/launch-sync). No business logic.
- **`main.c`** — the monolithic UI/controller. Owns all `Window`s (main list
  `MenuLayer`, full-screen alarm, per-timer detail/long-press menu, touch-dial
  time-edit window, transient "Started" confirmation, delete-confirm),
  click-config handlers, AppTimer-driven tick/redraw and repeat-buzz, and the
  AppMessage inbox/outbox. State lives in static globals (`s_timers`,
  `s_order`, `s_count`, ...). Orchestrates `timer_calc` + `timer_store` +
  `dial_touch`.
- **`dial_touch.c`/`.h`** — thin adapter exposing
  `dial_touch_create/destroy/enable/in_progress` to `main.c`, delegating to
  the vendored touch-dial widget under `#if PBL_TOUCH` (no-op on non-touch
  platforms).
- **`touch_dial/`** and **`multitap_keyboard/`** are vendored third-party
  widgets, not first-party code — `touch_dial` is GPLv3 (Andrew Howe,
  copyright header in `touch.h`, no LICENSE file), `multitap_keyboard` is
  Apache-2.0 (`multitap_keyboard/LICENSE`). Don't restyle their internals to
  match the rest of the codebase's conventions; treat them as upstream.
- No `worker_src/` — there is no background-worker binary.

### Phone side (`src/ts/`)

- **`timer_config.ts`** — the shared serialization contract: the RS/US-
  delimited string format mirroring the C struct (`MAX_TIMERS=16`,
  `NAME_MAX=31`, kept in sync with `timer_calc.h`), plus
  `hmsToSeconds`/`secondsToHms`/`sanitizeName`.
- **`config_clay.ts`** — the Clay config-page schema (timer list, sort-order
  radiogroup, running-first/auto-return/launch-sync/idle-exit toggles).
- **`config_timer_list.ts`** — the custom Clay `timerList` UI component.
- **`config_sync.ts`** — builds the AppMessage dict from `localStorage`-
  persisted settings, used to resend config when the watch asks for it on
  launch (a watchapp isn't always running to catch `webviewclosed`).
- **`add_timer.ts` / `update_timer.ts` / `delete_timer.ts`** — one inbound-
  from-watch operation each; each mutates the `timer_config` string and
  mirrors the change into `clay-settings` (so a later phone-side Save doesn't
  clobber a watch-originated change). All take injected `get`/`set` storage
  functions instead of touching `localStorage` directly — that's what makes
  them unit-testable without mocking the Pebble runtime.
- **`index.ts`** — entry point; wires `Pebble.addEventListener` for
  `appmessage`/`showConfiguration`/`webviewclosed`, dispatches inbound
  AppMessages to the right handler, builds the outbound
  `TimerConfig`/`SortOrder`/`AutoReturn`/`RunningFirst`/`IdleExitSec`/
  `LaunchSync`/`DefaultFinishAction` dict on Save.

AppMessage keys (declared in `package.json` under `pebble.messageKeys`, used
as `MESSAGE_KEY_*` in C): phone→watch config push is
`TimerConfig`/`SortOrder`/`AutoReturn`/`RunningFirst`/`IdleExitSec`/
`LaunchSync`/`DefaultFinishAction` (default "After finished" behavior,
Delete/Save, for newly created timers); watch→phone is `Request` (ask for
config), `AddTimer`/`AddTimerName`/`AddTimerId`, `DeleteTimer`,
`UpdateTimerIndex`/`UpdateTimerSeconds`/`UpdateTimerName` (one-way, no echo —
the watch already applied the change locally), and `CfgOpen` (tells the watch
the Clay page opened/closed, to pause/resume idle auto-exit). The phone
config is the single source of truth for naming/reordering, but
watch-originated create/adjust/delete syncs back to it (matched by the
persistent `id` each `Timer`/`TimerEntry` carries — see `tc_reconcile` in
`timer_calc.c` — not by list position).

All of this section's AppMessage keys are watch-only test hooks, never sent
or read by the phone app, and (per the "is this a testing hook, not
whether it changes real behavior" rule) are ALL only handled when the
build was made with `APP_TEST_HOOKS=1` in the environment (`APP_TEST_HOOKS=1
pebble build`) — a normal build still declares every key below (harmless,
unused integers/strings) but silently ignores the messages.

`TestSetTimerIndex`/`TestSetTimerState`/`TestSetTimerRemaining` are a
testing/screenshot backdoor (see `make send_emulator_set_timer` above) that
forces the timer at a raw list index into an exact state/remaining-time
combo, bypassing the normal start/pause/reset flow.

`TestSetTimerRemainingDisplay`/`TestSetTimerRemainingDisplayToleranceSec`
(per timer, targeted via the same `TestSetTimerIndex` field as above, sent
in the same message), `TestSetClockDisplay`/
`TestSetClockDisplayToleranceMinutes` (a `"HH:MM"` string), and
`TestSetLaunchElapsedDisplaySec`/`TestSetLaunchElapsedDisplayToleranceSec`
are explicit, per-value display overrides — this is what makes golden/
pixel-comparison screenshot testing deterministic, replacing the need to
`--mask-rect` those regions out of golden comparison at all. Each family
sets what its one display value shows (a timer's remaining/overtime text,
the bottom-bar/alarm-screen clock, the bottom bar's elapsed-since-launch)
completely independently of the others and of real app state — `main.c`'s
`effective_now_for(idx, t)` derives an "effective now" backwards from the
override (`end_time - override`) so every existing `tc_remaining_now()`/
`ml_row_colors()` formula stays untouched, only the "now" fed into it
changes; the clock/elapsed overrides are simpler direct substitutions.
Every other `now_s()` call site (expiry sweep, wakeup rearm, sort order,
persistence) is completely untouched by any of this, so an overridden
screenshot can never mask a real expiry/wakeup bug — only rendering is
affected.

The optional tolerance field in each family is the actual safety net: if
given, `inbox_received()` compares the requested value against the app's
own real ground truth (`tc_remaining_now()` for a timer, the real
clock-of-day, or the real `raw_launch_elapsed_s()`) at the moment the
override is set, and if it's out of tolerance, that one reading — and
only that one, not the whole screen — gets replaced with a `"BAD"` marker
instead of the (possibly stale) requested value. This is what lets a
`.seq` file *require* that a frozen-looking value stays close to what a
real device would actually be showing, rather than accepting an
arbitrarily-diverged guess: a sequence sends the value it expects
(exact, if backdoor-derived and time-independent; a generous estimate, if
following a real button-driven state transition whose exact timing isn't
known in advance) immediately before each `SCREENSHOT`, not once for the
whole run. Omitting a tolerance just sets a static display value with no
verification (e.g. the clock/elapsed overrides, which are always sent as
`"12:00"`/`0` with no tolerance, since — like the `FreezeDisplay`-era
`"12:00"` placeholder they replace — there's no "correct" clock or
elapsed-launch value to check against, only a stable one). A `TS_PAUSED`/
`TS_IDLE` timer's remaining value is a special case: `tc_remaining_now()`
returns the stored `t->remaining`/`t->duration` directly, ignoring `now`
entirely, so a display override has **no visual effect** on it at all —
only send one for a paused/idle timer if its exact value really is known
(otherwise skip it for that message; the value shown is unaffected either
way, and skipping avoids a spurious `"BAD"` marker on the detail window's
header, which doesn't share `ml_draw_detail_line()`'s guard against this).

`TestBlockedSystemWakeupMinuteOffsetsPos`/`...Neg` are handled the same
way (`APP_TEST_HOOKS=1`-gated). Each is a comma-separated list
of non-negative whole-minute offsets (e.g. `"0,2"`), simulating another
app's wakeup occupying those minutes *relative to whichever timer's own
end_time is currently being evaluated* — Pos counts minutes at/after that
reference minute, Neg counts minutes before it, split into two fields
specifically so parsing never has to handle a signed list. `test_wakeup_
schedule()` (`main.c`) intercepts the ~3 real `wakeup_schedule()` call
sites and refuses a request exactly like Pebble's real ±60s exclusion
window would if it lands near a configured offset, otherwise calls through
to the real API unchanged. Because the reference re-centers fresh on every
call, offset `"0"` alone keeps blocking the right timer's own minute even
as its end_time shifts (`+1 min`, `-1 min`) or as a *different* timer
becomes the current target — no reconfiguration needed between phases of a
multi-timer test. Sending either field re-triggers an immediate re-check
(`handle_wakeup_result(rearm_wakeup(), ...)`); an absent field leaves that
side's list unchanged, send an empty string to explicitly clear one side.
This exists specifically for
`tests/functional/sequences/walkthroughs/wakeup_conflict_*.seq` (see
"Tests" below); replaces the old ad hoc `// TEMP-TEST-FORCE` one-line edit
mentioned in the `tests/wakeup_test_app` note (and an earlier, less
realistic single-timer-id `TestForceWakeupFailFor` version of this same
hook) with something permanent, scriptable, and much closer to how the
real exclusion window actually behaves.

## Tests

- `tests/test_timer_calc.c` — plain `assert`-based C program, no framework,
  `#include`s/links `timer_calc.c` directly (see build command above).
- `tests/*.test.js` — Node's built-in `node:test` + `node:assert`, run
  against **compiled** `src/pkjs/*.js` (not `src/ts` directly), which is why
  `npm test` has a `pretest: tsc` step. No Pebble API mocking is used or
  needed: the modules under test take injected `get`/`set` storage functions,
  and tests supply an in-memory `Map`-backed `fakeStore` in place of
  `localStorage`.
- `tests/wakeup_test_app/` — a separate, bare-minimum Pebble project (own
  UUID/`.pbw`, since a watchapp is one process per package) for testing the
  wakeup-conflict feature against a REAL other app's wakeup, not the
  temporary `// TEMP-TEST-FORCE` hack sessions have used before (a one-line
  edit forcing `rearm_wakeup()` in `main.c` to always return
  `WAKEUP_ARM_FAILED`, reverted before finishing) - that hack is still fine
  for simpler self-only conflict simulation, but never exercises Pebble's
  actual cross-app wakeup exclusion or real pre-emption/graceful-close
  timing the way this app does. Entirely AppMessage-driven
  (`ScheduleWakeup`/`CancelWakeup` int keys) - see its own README for usage
  and an end-to-end cross-app conflict test recipe. Build/install it
  independently
  (`cd tests/wakeup_test_app && pebble build && pebble install --emulator
  emery --vnc`) alongside the main app in the same emulator.
- `tests/functional_framework/` — a generic (app-agnostic, copyable to other
  Pebble projects) bash interpreter for scripted emulator walkthroughs:
  flat plain-text `.seq` files (button presses, AppMessages, sleeps,
  screenshots, installs, raw `pebble` CLI passthrough, with `IMPORT` to
  share setup between sequences) run via its own `run_sequence.sh`. See its
  own README for the instruction-set reference. `tests/functional/` holds
  this app's own config (`app.conf`) and sequences (`sequences/common/` for
  shared setup like wipe+prep, `sequences/walkthroughs/` for actual test
  scenarios).

  **`tests/functional/run_sequence.sh <seq-file>` is the one entrypoint to
  run a single sequence for this app** - don't invoke
  `tests/functional_framework/run_sequence.sh` directly. It runs inside the
  `pebble-another-timer-tests` container image by default (see
  `container/README.md` for image setup) - reproducible clock, no races
  against a shared emulator - or natively against a shared host emulator
  with `--no-container`, which is the right choice for fast interactive
  dev/debugging but whose screenshots aren't reproducible run to run (see
  below). E.g.:
  ```bash
  tests/functional/run_sequence.sh \
    tests/functional/sequences/walkthroughs/create_and_start_timer.seq
  # or, natively:
  tests/functional/run_sequence.sh --no-container \
    tests/functional/sequences/walkthroughs/create_and_start_timer.seq
  ```
  `--touch` (needed for a sequence using the TOUCH/TOUCHDOWN/TOUCHUP/
  TOUCHMOVE/TOUCHSWEEP/TOUCHDRAG instructions, auto-detected by default in
  container mode) is incompatible with `--no-container` and rejected
  outright - a real touchscreen event only reaches the guest through a
  genuine SDL/X11 window (Xvfb+xdotool), which is container-only; there is
  no supported native equivalent (see `container/Containerfile`'s own
  comment).

  A native run is still subject to every gotcha in the `pebble-emulator`
  skill (idle-exit, `--vnc` consistency, `--app-uuid` matching) - the
  framework applies those structurally (every emulator-facing step gets
  `--emulator`/`--vnc` automatically) but doesn't remove the underlying
  constraints.

  `sequences/walkthroughs/wakeup_conflict_*.seq` cover the conflict-window
  feature end to end under its current single-Ok/Don't-exit/Exit-anyway
  design (silent auto-resolution when a free early-wake slot exists vs.
  the single-row "Ok" informational window when none does, the 2-row
  exit-guard re-prompt, the `handle_wakeup_result()` re-entrancy guard,
  multi-timer plan eviction, natural-fire-while-open, and pausing a timer
  that holds an accepted plan) using
  `TestBlockedSystemWakeupMinuteOffsetsPos`/`...Neg` (see above) instead of
  `tests/wakeup_test_app`'s real cross-app timing - deterministic and fast.
  Needs an `APP_TEST_HOOKS=1` build like every other sequence now does (see
  `wipe_and_prep.seq`'s own header comment - a normal build silently
  ignores every `Test*` AppMessage rather than erroring, so a walkthrough
  that never shows an expected conflict window is the first symptom to
  check this against).
  Written without a live emulator run available at the time (only read
  against the source, not confirmed on screen) - verify the exact
  row-index button sequences before trusting these as standing regression
  tests, per each file's own "unverified" note.

  The rest of `sequences/walkthroughs/` covers core, non-wakeup-conflict
  functionality along the same lines: run-control (pause/resume/stop,
  ±1 min, restart), natural expiry + the alarm queue, the "+ New timer"
  wizard (including discarding a draft), the long-press edit menu
  (duration/label/after-finished/vibration/sound), `RunningFirst`,
  `TimerConfig` reconcile preserving a running timer's live state across a
  same-id edit, idle-exit actually firing, and the empty-list state. Same
  "unverified against a live emulator at authoring time" caveat applies;
  a few (label rename, direct single-item delete) are deliberately scoped
  down to what's reachable with button-only input and no live
  confirmation of the multitap keyboard's exact controls - see those
  files' own header comments for what's intentionally left as a manual-only
  gap.

  **`tests/functional/run_all.sh` (the "does the whole suite pass" /
  CI / golden-approval entrypoint) is containerized-only - it has no
  native mode.** `tests/functional/run_sequence.sh --no-container` (see
  above) is still fully supported and is the right choice for fast
  interactive dev/debugging a single sequence, but its screenshots aren't
  reproducible run to run for anything the `TestSet*Display` override
  families (see above) don't cover, so it's never trusted as a real
  pass/fail verdict or as a source for approving a golden baseline -
  `update_golden()`/`promote_golden.sh`
  (`tests/functional_framework/lib/golden.sh`) mechanically refuse to do so
  from a native run. See `tests/functional/container/README.md` for setup.
  Any CI job for this project must call `run_all.sh`, not
  `run_sequence.sh --no-container`.
