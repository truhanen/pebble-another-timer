# wakeup_test_app

Bare-minimum companion Pebble app, used **only** for testing
`pebble-another-timer`'s wakeup-conflict resolution against a REAL other
app's wakeup — not the `TEMP-TEST-FORCE` hack used elsewhere in this repo's
manual test rounds (that hack only fakes `rearm_wakeup()` failing inside the
timer app itself; it never exercises Pebble's actual cross-app wakeup
exclusion, or real app-pre-emption/graceful-close timing).

It's a separate Pebble project (own UUID, own `.pbw`) because a watchapp is
one process per package — it can't be bundled inside the main app. Install
it alongside `pebble-another-timer` in the same emulator/watch; the two show
up as separate entries in the launcher.

Entirely AppMessage-driven — no on-watch interaction. The single status line
on screen exists only so a `pebble screenshot` can confirm what happened.

## Build & install

```sh
cd tests/wakeup_test_app
pebble build
pebble install --emulator emery --vnc
```

(See the `pebble-emulator` skill in the main project for `--vnc`/idle-exit
notes — they apply here too.)

## Usage

Message keys (declared in this project's own `package.json`, independent of
the main app's numbering) — check `build/js/message_keys.json` after a build
if these ever drift:

- `ScheduleWakeup` (int, key `10000`): schedule a real wakeup at
  `now + <value>` seconds. Replaces any wakeup this app already has pending
  (only one at a time). The app must actually be the current foreground app
  to receive this — launch/switch to it first.
- `CancelWakeup` (int, key `10001`, value ignored): cancel the pending
  wakeup, if any.

```sh
pebble send-app-message --emulator emery --vnc \
  --app-uuid 89a89b3c-f84e-4233-bb93-82a9a247aab8 --int 10000=60

pebble send-app-message --emulator emery --vnc \
  --app-uuid 89a89b3c-f84e-4233-bb93-82a9a247aab8 --int 10001=0
```

After scheduling, the app shows "Armed for HH:MM:SS (+Ns)" for ~2 seconds
then pops itself to the watchface — it has to actually leave the foreground
for its own wakeup to later be able to pre-empt whatever's running by then,
which is the entire point of this tool. When the wakeup fires, it relaunches
(pre-empting whatever app was in the foreground), vibrates once, shows
"Fired at HH:MM:SS", logs an `APP_LOG` line, then auto-exits the same way a
couple seconds later. Reopening it manually in between shows "Still armed
(id N)" if the wakeup hasn't fired yet, or "Idle" once it has (or after
cancelling).

## A real cross-app conflict test, end to end

1. Install both this app and the main `pebble-another-timer` app to the same
   emulator.
2. Switch to (or install-and-launch) this app; send it `ScheduleWakeup=N`
   for some N giving enough time to switch apps and act (60s is comfortable
   over manual `pebble emu-button`/`screenshot` round-trips).
3. Switch to `pebble-another-timer` and start a timer whose real end time
   lands within roughly 60 seconds of this app's scheduled fire time (the
   two just need to be close — Pebble's own exclusion window is ±60s around
   an existing wakeup's exact timestamp, not calendar-minute-aligned).
4. The main app's own `rearm_wakeup()` should now genuinely fail (a real
   conflict, not a hack) and open its wakeup-conflict window.
5. To also test the pre-emption/`deinit()` fallback path specifically (see
   `wakeup_conflict_remaining_sites` memory for the real-hardware incident
   this was built to help chase down): leave the main app's conflict
   unresolved (or resolved via "Keep app in foreground") and just wait for
   this app's own wakeup to fire — it will pre-empt the main app exactly
   like a real other app would, exercising the same graceful-close /
   `deinit()` path that's otherwise very hard to trigger on demand.

## Scripted version: `run_conflict_test.sh`

Doing the above by hand via interactive tool calls turned out to be too
timing-fragile to reliably land: each `pebble ... --vnc` call costs a real
~10-12s round trip, which eats directly into the same ±60s exclusion window
the whole test depends on - in practice the timer's own natural
`tick_cb`-driven fire kept winning the race against the colliding wakeup.
`run_conflict_test.sh` drives the entire sequence (wipe, install both apps,
schedule the wakeup, switch apps, create a timer with a duration tuned to
land just after the wakeup's fire time, resolve via "Keep app in
foreground", then poll with periodic screenshots) as one local script, with
no per-step tool-call latency:

```sh
cd tests/wakeup_test_app
./run_conflict_test.sh                # writes to ./conflict_test_<timestamp>/
./run_conflict_test.sh /tmp/my_run    # or an explicit output directory
```

It only *drives* the emulator and saves timestamped screenshots - it
doesn't judge the outcome itself. Review the screenshots afterward for:
whether a real conflict window actually appeared (`05_after_create.png`),
and whether the wakeup pre-empted the app while the timer was still
running vs. the timer's own natural fire winning first (the numbered
`10_poll_NN.png` series). The `WAKEUP_OFFSET_S`/`REAL_DURATION_S` constants
at the top are the two knobs to retune if the race keeps going the wrong
way - see the comment above them for how they were calibrated.
