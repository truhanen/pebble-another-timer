# functional_framework

A small, app-agnostic bash interpreter for driving Pebble-tool's QEMU
emulator through scripted walkthroughs: button presses, AppMessages,
sleeps, screenshots, installs, and raw `pebble` CLI passthrough, described
in flat plain-text "sequence" (`.seq`) files that can import each other.

This directory is meant to be copied as-is into another Pebble project -
nothing in here is specific to any one app. Everything app-specific (UUID,
default platform, whether to use `--vnc`, container image tag/build env)
lives in a small `--conf` file you supply, and your own `.seq` files live
outside this directory (see `tests/functional/` in this repo for a worked
example). Container-based execution (deterministic, parallel-safe) is
built in too - see `container/README.md`.

## Usage

```sh
tests/functional_framework/run_sequence.sh \
  --conf tests/functional/app.conf \
  --seq  tests/functional/sequences/walkthroughs/create_and_start_timer.seq
```

Options:
- `--platform NAME` - override the conf file's platform for this run.
- `--vnc` / `--no-vnc` - override the conf file's default.
- `--out-dir DIR` - base directory for run output (default: `out/` next to
  the conf file). Each run's output goes under
  `<out-dir>/<RUN_ID>/<sequence-name>/` - timestamp first, sequence name
  nested under it - containing `run.log` and any screenshots taken, so a
  whole batch of sequences run together shares one browsable timestamped
  directory instead of scattering across a separate one per sequence.
- `--run-id ID` - use this instead of generating a fresh
  `date +%Y%m%d_%H%M%S` timestamp for the run's output directory - what
  `run_batch.sh` uses to group every sequence in one batch under the same
  `<RUN_ID>`.
- `--continue-on-error` - keep executing after a failed step instead of
  stopping immediately, printing a pass/fail summary at the end.
- `--golden-dir DIR` - approval-based regression testing against a
  committed baseline. See "Golden-file (approval-based) regression
  testing" below.
- `--update-golden` - with `--golden-dir`, approve this run's output as
  the new baseline instead of comparing against the existing one.
- `--fuzz PERCENT` - tolerance for golden screenshot comparison (default
  `0`, exact match). Only meaningful with `--golden-dir`.
- `--mask-rect WxH+X+Y` - black out this pixel region on every screenshot
  this run captures, baked into the saved PNG at capture time. See the
  script's own `--help` for the full rationale and how a single
  `SCREENSHOT`'s own `--mask-rect` interacts with this whole-run default.
- `--container` / `--no-container` - override the conf file's `CONTAINER`
  default for this run. See "Container-based execution" below.
- `--image-tag TAG` - override the conf file's `IMAGE_TAG` (container mode
  only).
- `--touch` - force touch-input mode on (container-only; normally
  auto-detected - see `container/README.md`).
- `--build-only DIR` - build once inside the container image and copy the
  result to `DIR/build`, instead of running a sequence. Used by
  `run_batch.sh`'s batch mode; not meant for standalone use.

Exit code is non-zero if any step failed (or, with `--golden-dir` and no
`--update-golden`, if the golden comparison itself failed).

## Conf file format

Plain `KEY=value` lines, sourced directly by the runner:

```sh
APP_UUID=1df6fc5c-261d-49c7-b339-6ea60cbe6649
PLATFORM=emery
VNC=1

# Optional - container-based execution (see container/README.md):
CONTAINER=1
IMAGE_TAG=my-app-functional-tests
SDK_VERSION=4.33.1
BUILD_ENV="APP_TEST_HOOKS=1"

# Optional - defaults for --golden-dir / run_batch.sh's sequences directory,
# both resolved relative to this conf file's own directory unless absolute:
GOLDEN_DIR=golden
SEQ_DIR=sequences/walkthroughs
```

`APP_UUID` is required (used for `APPMSG` steps); `PLATFORM` defaults to
`emery`, `VNC` defaults to `1`.

The rest are all optional, and only matter once you adopt container-based
execution or `run_batch.sh`:
- `CONTAINER` - `0` or `1`, default `0`. Makes this conf containerized by
  default (see `--container`/`--no-container` above to override per-run).
- `IMAGE_TAG` - the Podman/Docker image tag to use/build. Default:
  `pebble-functional-tests-$APP_UUID`.
- `SDK_VERSION` - required when building the container image
  (`container/build_image.sh`) - the Pebble SDK version to install in it.
- `BUILD_ENV` - newline/space-separated `KEY=VAL` pairs exported before
  `pebble build` inside the container (e.g. a project's own test-hooks
  build flag). Optional; nothing is exported if unset.
- `GOLDEN_DIR` - default for `--golden-dir` when not passed explicitly,
  resolved relative to the conf file's own directory (or used as-is if
  absolute).
- `SEQ_DIR` - default sequences directory for `run_batch.sh`, resolved the
  same way. Default if unset: `sequences/walkthroughs`.

## Running the whole suite (`run_batch.sh`)

```sh
tests/functional_framework/run_batch.sh --conf tests/functional/app.conf \
  [--seq-dir DIR] [-j N] [--pattern GLOB] [--golden-dir DIR] \
  [--update-golden] [--continue-on-error] [--container] [--no-container]
```

Runs every `.seq` file in `--seq-dir` (default: the conf file's own
`SEQ_DIR`), `-j` at a time (default `2`), reporting a pass/fail summary at
the end and exiting non-zero if anything failed. In container mode, builds
the app ONCE for the whole batch and shares that build across every
sequence's own container (see `container/README.md`) instead of repeating
`npm install`/`pebble build` once per sequence. See the script's own
`--help` for the full flag reference and failure-reporting semantics.

## Container-based execution

`CONTAINER=1` (in the conf file, or `--container` on the command line)
runs a sequence inside a Podman/Docker image instead of against a
host-managed emulator - deterministic clock (no `--mask-rect`/pinning
gymnastics needed), and safe to parallelize (`run_batch.sh` with `-j > 1`).
Native mode (the default) needs no container setup at all, at the cost of
non-reproducible screenshots - golden-baseline approval
(`--update-golden`/`promote_golden.sh`) refuses to run from a native
invocation for exactly this reason (see "Golden-file (approval-based)
regression testing" below). See `container/README.md` for full setup,
usage, and the touch-input/memory/architecture notes.

## Sequence file format

One instruction per line. The first whitespace-separated token is the
keyword; the rest is arguments, parsed with normal shell quoting/word
splitting (so `"a b"` is one argument) and `$VAR`/`${VAR}` expansion.
Blank lines and lines starting with `#` are ignored.

| Keyword | Args | Effect |
|---|---|---|
| `IMPORT` | `path` | Inlines another `.seq` file's instructions in place. A relative `path` is resolved against the importing file's own directory; an absolute path is used as-is. Recursive; cyclic imports are an error. |
| `BUILD` | - | `pebble build` |
| `INSTALL` | `[platform]` | `pebble install --emulator <platform\|conf default> [--vnc]` |
| `WIPE` | - | `pebble wipe` (no `--emulator`/`--vnc` - that subcommand doesn't take them) |
| `KILL` | - | `pebble kill` (also takes no `--emulator`/`--vnc`; never fails the sequence, even if nothing was running) |
| `BUTTON` | `button action` | `pebble emu-button ... <action> <button>` (raw push/release), e.g. `BUTTON select push` |
| `TAP` | `button [hold_s]` | `pebble emu-button ... click <button> --duration <ms>` (default `hold_s` `0.15`) |
| `LONGPRESS` | `button [hold_s]` | like `TAP` but default hold `0.7`s - long enough to clear this app's long-click threshold |
| `SLEEP` | `seconds` | `sleep seconds` |
| `SCREENSHOT` | `label [--mask-rect WxH+X+Y]` | `pebble screenshot --no-open ...`, saved as `<NN>_<label>.png` in the run's output dir (auto-numbered). An optional `--mask-rect` paints a solid black rectangle over that region IN THE SAVED FILE, right after capture (see `run_screenshot`/`_mask_rect_apply` in `lib/steps.sh`) - overrides `run_sequence.sh`'s own whole-run `--mask-rect` default for just this one screenshot, letting one screenshot mask a region of genuine, expected non-determinism (e.g. the OS watchface's rotating hint text) without forcing every other screenshot in the sequence to mask that same region too. |
| `APPMSG` | raw `send-app-message` args | `pebble send-app-message --emulator ... --app-uuid <conf uuid> <args>`, e.g. `APPMSG --int 10011=0` |
| `TOUCH` | `x y [hold_s]` | A real touchscreen tap-and-hold at content-relative coordinates, via `xdotool` against the emulator's own real window. Requires `--no-vnc` and an X11 `$DISPLAY` (Xvfb) - **container-only**, not supported natively; see `container/README.md`'s TOUCH section. `hold_s` (default `0.6`) must be a genuine hold, not a tap - see `run_touch` in `lib/steps.sh` for why. Atomic (releases before returning) - can't itself produce a screenshot of a mid-gesture UI state; use `TOUCHDOWN`/`TOUCHUP` for that. |
| `TOUCHDOWN` | `x y` | Same mechanism as `TOUCH`, but presses and HOLDS - use with a `SCREENSHOT` step before the matching `TOUCHUP` to capture UI that's only shown while a touch is actively held (e.g. this app's round touch dial). Same container-only requirements as `TOUCH`. |
| `TOUCHUP` | - | Releases a touch started by `TOUCHDOWN`, at its same position. |
| `TOUCHMOVE` | `x y` | Moves an already-down touch (from `TOUCHDOWN`) to new coordinates, generating one `TouchEvent_PositionUpdate` - a building block for a custom drag path; most sequences want `TOUCHDRAG` instead. |
| `TOUCHSWEEP` | `cx cy radius start_deg end_deg [steps]` | Moves an already-down touch (from `TOUCHDOWN`) along a circular arc, without releasing - the split form of `TOUCHDRAG`, for a `SCREENSHOT` of the dial mid-drag before a later `TOUCHUP` commits it. Same angle convention as `TOUCHDRAG`. |
| `TOUCHDRAG` | `cx cy radius start_deg end_deg [steps] [hold_s]` | A circular-arc drag (press, sweep through `steps` waypoints from `start_deg` to `end_deg`, hold, release) - this app's touch dial reads only the angle from centre, so a realistic drag must move along an arc, not a straight chord (which would cut through the centre "Cancel" zone). Degrees: `0` = straight up, increasing clockwise; `end_deg` may go negative/past 360 to express continued rotation past the 0 mark. Atomic (releases before returning) - use `TOUCHDOWN`/`TOUCHSWEEP`/`TOUCHUP` instead for a mid-drag screenshot. See `run_touchdrag` in `lib/steps.sh` for the full mechanics. |
| `CMD` | `subcommand [args...]` | escape hatch: `pebble <subcommand> --emulator ... [--vnc] <args>` for anything not covered above |
| `LOG` | free text | echoed into the run log, no side effect |
| `VAR` | `NAME=value` | sets a shell variable usable as `$NAME` in later lines of the same run |

Every emulator-facing keyword (`INSTALL`/`WIPE`/`BUTTON`/`TAP`/`LONGPRESS`/
`SCREENSHOT`/`APPMSG`/`CMD`) consistently appends `--emulator <platform>`
and, when enabled, `--vnc` - you never need to (and shouldn't) pass those
yourself in a `.seq` file.

On a failed step, the runner stops immediately and reports the *original*
source file and line number the failing instruction came from (even if it
was reached via an `IMPORT` chain), unless `--continue-on-error` is given.

## Golden-file (approval-based) regression testing

`--golden-dir DIR` compares a passing run's screenshots and run log
against a committed baseline instead of just leaving them for human
review, organized as one subdirectory per sequence under `DIR`, named
after the `.seq` file's own basename (e.g. `wakeup_conflict_basic_
resolutions.seq` -> `DIR/wakeup_conflict_basic_resolutions/`):

```sh
# First time (or after a deliberate UI change): approve the current output
tests/functional_framework/run_sequence.sh \
  --conf tests/functional/app.conf \
  --seq  tests/functional/sequences/walkthroughs/create_and_start_timer.seq \
  --golden-dir tests/functional/golden --update-golden

# Later: verify nothing changed (e.g. after a refactor, or in CI)
tests/functional_framework/run_sequence.sh \
  --conf tests/functional/app.conf \
  --seq  tests/functional/sequences/walkthroughs/create_and_start_timer.seq \
  --golden-dir tests/functional/golden
```

If you already have a passing run's output sitting in `out/` (from a plain
run, or one you ran earlier without `--golden-dir` at all) and just want
to approve THAT output as golden, `promote_golden.sh` does the same copy
as `--update-golden` without re-running anything against the emulator -
useful since a live run here means a full wipe+install+walkthrough, easily
30-90s+ per sequence:

```sh
tests/functional_framework/promote_golden.sh \
  --run-dir tests/functional/out/<RUN_ID>/create_and_start_timer \
  --golden-dir tests/functional/golden
```

It refuses a run whose `run.log` doesn't end in `sequence PASSED` unless
you pass `--force` - promoting a failed or inconclusive run's output as
the approved baseline is very likely a mistake.

**Both `--update-golden` and `promote_golden.sh` also refuse a run that
wasn't produced under a containerized/pinned-clock harness** (see the
next section on why that matters). Concretely: `update_golden()`
(`lib/golden.sh`) looks for a `.containerized` marker file in the run
directory being promoted from, which `run_sequence.sh` only writes when
the environment variable `PEBBLE_TEST_CONTAINERIZED=1` was set for that
run - which happens automatically whenever you run via `CONTAINER=1`/
`--container` (see `container/entrypoint.sh`, alongside its libfaketime
setup, for the mechanism). `PEBBLE_TEST_ALLOW_NATIVE_GOLDEN=1` is a
deliberately loud, env-var-only (not a flag) escape hatch for a genuine
one-off exception - there should be no routine reason to use it.

The sequence itself must make its own live displays (clocks, elapsed
counters, running timers, ...) deterministic before you can trust
`--golden-dir` against it - see the note below for how this app does it.

Since a golden directory is just files, commit it to git - "did this
sequence's output change since an earlier commit" is then just `git show
<commit>:tests/functional/golden/<sequence-name>/<file>` plus this same
comparison; bisecting *which* commit changed something is an ordinary
`git bisect` loop around a `--golden-dir` run. No separate visual-
regression framework or dashboard needed for either.

Mechanics (`lib/golden.sh`):
- Screenshots are compared pixel-for-pixel (or within `--fuzz PERCENT`)
  via ImageMagick's `compare` (`magick compare` on ImageMagick 7, a
  standalone `compare` binary on 6 - either is detected automatically).
  A mismatching, missing, or unexpectedly-new screenshot all count as a
  failure; a per-file visual diff image is written to
  `<run_out_dir>/diffs/` for anything that didn't match.
- `run.log` is compared with plain `diff`, after stripping each line's
  `[HH:MM:SS]` prefix (the real wall-clock time the *host* ran that step
  at - unrelated to the emulator's own clock) from both sides first, so
  two runs of the same sequence produce byte-identical logs regardless of
  when either actually ran.
- **The runner itself does NOT make any live display deterministic for
  you** - any screen showing the live clock (a watchface, a bottom status
  bar, a running timer's countdown, ...) would otherwise never compare
  equal between two golden runs made at different real times, regardless
  of whether anything actually regressed. Three approaches exist, ranked
  by preference:
  1. **An app-side test hook that overrides what gets DRAWN, decoupled
     from the real clock** (this app's `TestSetTimerRemainingDisplay`/
     `TestSetClockDisplay`/`TestSetLaunchElapsedDisplaySec` AppMessage
     families - see `main.c`'s `effective_now_for()` and CLAUDE.md). The
     clearly best option where available: it never touches the real
     emulator/system clock at all, so it can't create the discontinuity
     the two approaches below are vulnerable to, and it keeps working
     identically for a sequence that ALSO waits for a real timer to fire
     (expiry/wakeup logic always reads the real clock regardless of this -
     only rendering is affected). Each override is independently checked
     against the app's own real ground truth at the moment it's set, with
     a tolerance the sequence controls, so a screenshot taken too long
     after setting it gets a visible failure marker instead of a silently
     stale value - a sequence's own `.seq` file sends the override (plus
     tolerance) it needs immediately before each `SCREENSHOT`, rather than
     one blanket freeze for the whole run. Worth building the equivalent
     for any project maintaining its own app - a small, narrowly-scoped,
     ground-truth-checked rendering override is far less fragile than
     either clock-pinning approach below.
  2. **Containerized, via `libfaketime`** (`CONTAINER=1`/`--container`,
     see `container/README.md`) - pins the clock for every process in the container to a
     fixed absolute instant, then lets it tick forward normally. Doesn't
     break sequences that wait for a real timer to fire (unlike approach 3
     below), and is the only path `--update-golden`/`promote_golden.sh`
     will accept by default (see above) - worth having regardless of
     approach 1, since it's also what makes screenshots reproducible for
     anything an app-side hook doesn't (or can't) cover.
  3. **Native, via `CMD emu-set-time HH:MM:SS`** (a real `pebble-tool`
     subcommand, independent of app code). Has a real limitation:
     fundamentally incompatible with any sequence that waits for a REAL
     timer to actually fire - pinning the DISPLAYED clock backward to a
     fixed reference after pebble-tool's own connection-triggered resync
     had already moved it forward to the real host time (confirmed:
     TAP/APPMSG trigger that resync, SCREENSHOT/emu-set-time itself do
     not - see `lib/steps.sh`) creates a backward-then-forward clock
     discontinuity that Pebble OS's real wakeup subsystem interprets as
     the watch having been off, producing the genuine system "While your
     Pebble was off..." screen instead of the app's own alarm screen.
     Only reach for this if neither of the above is available.
  This app's own `FAKE_TIME=1` build flag (`wscript`) was considered as a
  fourth option instead, but verified to currently be dead code - it
  defines `USE_FAKE_TIME`, but nothing in `src/c/` reads it - so relying
  on it would silently accomplish nothing; don't reintroduce that mistake
  if revisiting this.
- **Masking a region (`--mask-rect`) hides real regressions there too, not
  just noise** - it's a blunt instrument, not a precision one, and this
  app no longer needs it for anything by default now that approach 1 above
  covers every live display it has. Prefer extending the app-side freeze
  hook to a new display before reaching for a mask; keep `--mask-rect`
  available as a fallback for something a freeze hook can't reach (a
  third-party/OS-drawn element, for instance).

## What this v1 does not do

- Touch/dial input (`TOUCH`) is supported, but container-only (see the
  instruction table above and `container/README.md`) - there
  is no native equivalent.
