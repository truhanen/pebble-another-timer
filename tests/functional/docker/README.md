# Running functional tests in containers

Runs `tests/functional_framework`'s sequences inside Podman/Docker
containers instead of directly on the host - either one sequence at a
time (`run_container.sh`) or many at once in parallel
(`run_all_parallel.sh`), each in its own fully isolated container instead
of sharing one host-managed emulator instance.

**This is the REQUIRED path for `tests/functional/run_all.sh`, CI, and any
golden-baseline approval** - not merely the recommended one. A native run
against a shared host emulator is still fully supported and useful for
fast single-sequence interactive dev/debugging (attaching VNC, poking at
emulator state by hand - see `tests/functional_framework/README.md`), but
its screenshots are not reproducible run to run (see "Why this exists at
all" below), so `run_all.sh` no longer has a native mode at all, and
`update_golden()`/`promote_golden.sh` mechanically refuse to approve a
golden baseline from anything but a containerized run (a `.containerized`
marker file, written only when this harness's `PEBBLE_TEST_CONTAINERIZED=1`
is set - see `run-sequence-in-container.sh`). If you're setting up CI for
this project, its job MUST call `run_all.sh` (which always runs
containerized), not `tests/functional_framework/run_sequence.sh` directly
against a native emulator.

## One-time setup

```sh
# On Apple Silicon: make sure the Podman machine has Rosetta enabled and
# enough memory. `podman machine init --rosetta` if creating a fresh one;
# for an existing machine:
podman machine set --memory 8192   # see "Memory" below for why 8GB
podman machine start

# Build the image (from the repo root):
podman build --platform linux/amd64 -t pebble-another-timer-tests \
  -f tests/functional/docker/Containerfile .
```

Rebuild the image whenever `Containerfile` or
`run-sequence-in-container.sh` changes; you do NOT need to rebuild when
the app's own source changes - `run_container.sh` mounts the repo fresh
into every container run (see Containerfile's own comments for why: a
build baked into the image would go stale immediately, and copying at
container-start time is what makes parallel runs safe against each other
in the first place).

## Usage

```sh
# One sequence:
tests/functional/docker/run_container.sh \
  tests/functional/sequences/walkthroughs/create_and_start_timer.seq

# All of them, N at a time:
tests/functional/docker/run_all_parallel.sh -j 4

# A subset, N at a time:
tests/functional/docker/run_all_parallel.sh -j 4 --pattern 'wakeup_conflict_*'

# Golden testing works the same as natively (see the main framework's own
# README) - --golden-dir's host path is mounted into the container
# automatically. On macOS, that path MUST be somewhere under $HOME (this
# example qualifies) - a bare /tmp/... path fails with `Error: statfs
# ...: no such file or directory`, since /tmp isn't in Podman's applehv
# machine's default shared-mount scope. Live-verified; not something this
# script can detect or fix in advance. No --mask-rect needed - every
# sequence freezes its own live displays deterministic by default (see
# sequences/common/wipe_and_prep.seq / main.c's display_now()).
tests/functional/docker/run_container.sh \
  tests/functional/sequences/walkthroughs/create_and_start_timer.seq \
  --golden-dir tests/functional/golden

# The full gate (what CI should run): every sequence.
tests/functional/run_all.sh --golden-dir tests/functional/golden
```

## TOUCH (real touchscreen input) - opt-in, container-only

`--touch` (on `run_container.sh`, or wired through `run_all_parallel.sh`'s
own passthrough if you add it there) makes a sequence's `TOUCH x y [hold_s]`
instruction (see `functional_framework/README.md`) actually work: it starts
Xvfb inside the container and runs the emulator without `--vnc`, so
qemu-pebble opens a real SDL/X11 window that `xdotool` can deliver genuine,
correctly-hit-tested touch events into. This only works this way -
qemu's own `--vnc` framebuffer (what every other sequence uses) never
delivers touch input to the guest at all, and there is no supported way to
do this natively on a real desktop without either moving your actual mouse
cursor (disruptive to whoever's sitting at that machine) or granting OS-
level automation permissions to whatever runs the test (macOS
Accessibility, for `cliclick`) - a container's own Xvfb has neither problem,
since nothing real is looking at "the cursor" there.

```sh
tests/functional/docker/run_container.sh \
  tests/functional/sequences/walkthroughs/some_touch_sequence.seq --touch
```

**Performance: this is deliberately opt-in, not the default for every
sequence.** Most existing sequences are button/AppMessage-driven and have
no need for a real window at all, so they keep using the fast, lightweight
`--vnc`-only path unchanged. A `--touch` run pays a small, one-time-per-
container cost instead:
- Starting Xvfb and confirming it's up adds on the order of a second (live-
  verified near-instant; the entrypoint polls `xdpyinfo` rather than
  assuming a fixed sleep, since startup time isn't a hard guarantee across
  hosts/load).
- Running qemu without `--vnc` means it renders through a real window
  instead of just serving frames to a VNC client - at this app's tiny
  200x228 display, not a meaningful CPU cost in practice.
- A modest extra memory footprint per container (Xvfb itself is cheap, tens
  of MB) - additive on top of the existing parallel-container memory
  ceiling (see "Memory" below), so a batch mixing `--touch` containers with
  regular ones may need a lower `-j` or more machine memory to hold the same
  parallelism it had before.

None of this affects a sequence that doesn't use `TOUCH` - it never sets
`PEBBLE_TEST_TOUCH`, so it never starts Xvfb and keeps installing with
`--vnc` exactly as before.

Output lands in `tests/functional/out/` exactly like a native run - same
`<RUN_ID>/<sequence-name>/` layout, same `run.log`/screenshot naming - so
anything written against native output (promote_golden.sh, a CI artifact
step, ...) works unchanged against container output too.

## Why this exists at all

- **The emulator's displayed clock is deterministic without any
  `--mask-rect`/pinning gymnastics.** Every container runs under
  `libfaketime`, frozen to a fixed start instant that then ticks forward
  normally - see `run-sequence-in-container.sh`'s own comment for why
  this succeeds where the native framework's `emu-set-time`-based
  approach failed (it broke real-alarm-firing sequences; this doesn't,
  live-verified against `alarm_multiple_queue.seq` and others).
- **Parallelism.** Each container gets its own private copy of the
  source, its own qemu-pebble/pypkjs pair on isolated container-internal
  ports, and its own `/tmp` scratch build - so N sequences can run at
  once with no shared state to race on, unlike native runs (which all
  fight over one host-managed emulator instance; running two
  `run_sequence.sh` invocations concurrently against it is explicitly
  unsafe - see the main framework's own docs).

## Memory

**Podman's default machine memory (2GiB on a fresh `podman machine
init`) is NOT enough for more than one or two containers at once.**
Live-verified: 6 parallel containers (each doing its own `npm install` +
TypeScript compile + `pebble build` + qemu-pebble/pypkjs) reliably failed
3 of 6 with a plain "TypeScript compilation failed" under memory pressure
at 2GiB, and passed 6 of 6 at 8GiB with nothing else changed. If
`run_all_parallel.sh` jobs fail with build errors (not emulator/screenshot
errors) under load but pass individually, this is almost certainly why -
raise the machine's memory (`podman machine set --memory <MB>`, machine
must be stopped first) rather than assuming it's a code bug. There's no
fixed formula for how much `-j` a given memory budget supports (depends on
host CPU too) - if you hit this, lower `-j` or raise memory, whichever is
easier, and retry.

## Why `linux/amd64`, not the host's native architecture

`pypkjs` (which runs this app's actual PebbleKit JS inside the emulator)
depends on `stpyv8`, a V8 binding. stpyv8 publishes prebuilt wheels for
macOS (x86_64+arm64), Windows, and Linux - but only `manylinux_x86_64` for
Linux, no aarch64/arm64 Linux wheel at all (confirmed against PyPI's own
file listing), and building it from source means building V8 itself.  So
this image is always `linux/amd64`, even on an Apple Silicon host - which
works well specifically because Podman's `applehv` machine provider
supports Rosetta-translated amd64 containers (`podman machine inspect`
should show `"Rosetta": true`), giving near-native performance rather than
slow QEMU user-mode binfmt translation. If Rosetta isn't available/enabled
on a given host, amd64 containers still work, just markedly slower.
