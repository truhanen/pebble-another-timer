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

`SetTimerIndex`/`SetTimerState`/`SetTimerRemaining` are watch-only, never sent
or read by the phone app: a testing/screenshot backdoor (see
`make send_emulator_set_timer` above) that forces the timer at a raw list
index into an exact state/remaining-time combo, bypassing the normal
start/pause/reset flow.

## Tests

- `tests/test_timer_calc.c` — plain `assert`-based C program, no framework,
  `#include`s/links `timer_calc.c` directly (see build command above).
- `tests/*.test.js` — Node's built-in `node:test` + `node:assert`, run
  against **compiled** `src/pkjs/*.js` (not `src/ts` directly), which is why
  `npm test` has a `pretest: tsc` step. No Pebble API mocking is used or
  needed: the modules under test take injected `get`/`set` storage functions,
  and tests supply an in-memory `Map`-backed `fakeStore` in place of
  `localStorage`.
