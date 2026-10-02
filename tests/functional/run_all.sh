#!/usr/bin/env bash
# Runs every functional-test sequence under sequences/walkthroughs/,
# containerized, across several podman containers AT ONCE - the "does the
# whole suite pass" / CI / golden-approval entrypoint for this project.
#
# Container mechanics (see run_sequence.sh/container/Containerfile): each
# container gets its own private copy of the source
# (container/run-sequence-in-container.sh) and its own qemu-pebble/pypkjs
# pair on isolated container-internal ports, so there's no shared state
# between parallel jobs to race on.
#
# CONTAINERIZED EXECUTION ONLY - deliberate, not a missing feature. A
# native run's screenshots aren't reproducible run to run (the emulator's
# displayed clock/elapsed counters depend on real wall-clock time with no
# pinning - see wipe_and_prep.seq's own comment for why pinning it
# natively was tried and reverted), which would make this script's own
# pass/fail verdict meaningless. For a fast single-sequence run for
# interactive dev/debugging (attaching VNC, poking at emulator state by
# hand, avoiding container image rebuilds), use `run_sequence.sh
# --no-container` against your own host emulator instead - fully
# supported for that, just never authoritative for "does this pass" or
# for approving a golden baseline (update_golden() in lib/golden.sh
# enforces this independently of this script).
#
# Usage:
#   tests/functional/run_all.sh [-j N] [--pattern GLOB]
#                                [--golden-dir DIR] [--update-golden]
#                                [--continue-on-error]
#
# Flags:
#   -j N                   How many containers run at once. Default: 2
#                          (lowered from 4 on 2026-10-01 - see "Why -j2"
#                          below). A qemu-pebble instance itself is
#                          lightweight (Cortex-M33 TCG emulation), so this
#                          is bounded by host CPU/memory for N concurrent
#                          Rosetta-translated x86_64 containers, not by
#                          the emulator - raise it if the host has room.
#
#   --pattern GLOB         Restrict which sequences run, matched against
#                          each .seq file's basename (e.g.
#                          'wakeup_conflict_*' for just that family).
#                          Default: '*' (everything).
#
#   --golden-dir DIR       Base dir of golden baselines, one subdirectory
#                          per sequence, ALWAYS compared against (or
#                          approved into, with --update-golden) - this
#                          script has no "skip verification" mode. Only
#                          override this to compare/approve against some
#                          other location. Default: tests/functional/
#                          golden (this project's own, committed baseline
#                          set).
#
#   --update-golden        APPROVE each sequence's own output as the new
#                          baseline under --golden-dir, instead of
#                          comparing against the existing one (overwrites
#                          it).
#
#                          Comparison is always exact (fuzz=0, no
#                          tolerance) - this script has no --fuzz flag,
#                          by design: every sequence already makes its own
#                          live displays deterministic via app-side test
#                          hooks or per-screenshot masking (see
#                          tests/functional_framework/README.md's
#                          SCREENSHOT row), this project's standard for
#                          getting a real exact match rather than papering
#                          over drift with fuzz. The underlying framework
#                          still supports --fuzz for a project that needs
#                          it - see tests/functional_framework/
#                          run_sequence.sh --help.
#
#   --continue-on-error    Forwarded to each sequence's own step loop, so
#                          a failed STEP within one sequence doesn't stop
#                          that sequence early either. This script itself
#                          always runs every matching sequence regardless
#                          of this flag - see "Failure behavior" below.
#
# Why -j2 (lowered from 4, 2026-10-01):
#   Several sequences carry tight, real-wall-clock-timing-derived
#   screenshot assertions (e.g. run_control_plus_minus.seq). -j4 was
#   observed to occasionally starve a container badly enough under real
#   host CPU contention that a short-duration timer fired mid-sequence
#   well before its assumed real-time budget - not a few-seconds
#   tolerance miss but an entirely different, unrecoverable screen state.
#   -j2 leaves meaningfully more CPU headroom per container. Raise back
#   to 4+ only on a host with room to spare, and expect that failure
#   class to resurface if so.
#
# The container image must already be built - see container/Containerfile's
# own header.
#
# Failure behavior:
#   Failing sequences do NOT stop this script - it always runs every
#   matching sequence and reports a summary at the end, exiting non-zero
#   only if at least one sequence failed. --continue-on-error is a
#   different granularity (see the flag list above): it keeps a single
#   sequence's OWN step loop going after a failed step, rather than
#   aborting that one sequence early.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SEQ_DIR="$REPO_ROOT/tests/functional/sequences/walkthroughs"
RUN_SEQUENCE="$SCRIPT_DIR/run_sequence.sh"

usage() {
  sed -n '2,97p' "$0" | sed 's/^# \{0,1\}//'
}

JOBS=2
PATTERN='*'
GOLDEN_DIR="$REPO_ROOT/tests/functional/golden"
UPDATE_GOLDEN=0
PASSTHROUGH=()

while [ $# -gt 0 ]; do
  case "$1" in
    -j) JOBS="$2"; shift 2 ;;
    --pattern) PATTERN="$2"; shift 2 ;;
    --golden-dir) GOLDEN_DIR="$2"; shift 2 ;;
    --update-golden) UPDATE_GOLDEN=1; shift ;;
    --continue-on-error) PASSTHROUGH+=(--continue-on-error); shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

# Golden comparison always happens (or approval, with --update-golden) -
# no "skip verification" mode, see the flag list above. Fuzz is always 0
# (exact match) - no --fuzz flag, see the flag list above.
PASSTHROUGH+=(--golden-dir "$GOLDEN_DIR" --fuzz 0)
[ "$UPDATE_GOLDEN" = "1" ] && PASSTHROUGH+=(--update-golden)

SEQS=("$SEQ_DIR"/$PATTERN.seq)
if [ ! -e "${SEQS[0]}" ]; then
  echo "No sequences matched pattern '$PATTERN' in $SEQ_DIR" >&2
  exit 1
fi

JOB_DIR="$(mktemp -d)"
trap 'rm -rf "$JOB_DIR"' EXIT

# One shared timestamp for the whole batch, generated HERE on the host
# (never touched by any container's own libfaketime) and exported so every
# job script below inherits it, which run_sequence.sh then forwards into
# its container as RUN_ID_OVERRIDE - see container/run-sequence-in-
# container.sh's own comment for the full mechanism. Without this, each
# container generates its own independent RUN_ID and the batch's output
# scatters across dozens of unrelated top-level out/ directories instead
# of landing under one shared, browsable one.
export RUN_ID_OVERRIDE="$(date '+%Y%m%d_%H%M%S')"
echo "Batch output directory: $REPO_ROOT/tests/functional/out/$RUN_ID_OVERRIDE/"

# Build ONCE for this whole batch (inside the same image every sequence
# container uses, so identical SDK/toolchain - no version-drift risk)
# instead of once per sequence container - every one of them would
# otherwise independently npm-install + pebble-build the exact same
# source under the exact same APP_TEST_HOOKS, producing identical output
# regardless of which container does it. See run_sequence.sh's own
# --build-only/--prebuilt-build comments and container/run-sequence-in-
# container.sh's PREBUILT_BUILD handling for the rest of this mechanism.
# Gitignored scratch dir (bare "build" in .gitignore already covers any
# path component named that), cleaned up on exit alongside JOB_DIR.
SHARED_BUILD_DIR="$REPO_ROOT/tests/functional/container/build/$RUN_ID_OVERRIDE"
trap 'rm -rf "$JOB_DIR" "$SHARED_BUILD_DIR"' EXIT
echo "Building once (shared across all $JOBS parallel containers)..."
"$RUN_SEQUENCE" --build-only "$SHARED_BUILD_DIR"

# One small job script per sequence (rather than trying to pass an array
# of PASSTHROUGH args through xargs -I{} directly, which mangles multi-
# word arguments) - each writes its own pass/fail marker file, since xargs
# -P workers' exit codes aren't otherwise easy to collect back reliably.
i=0
for seq in "${SEQS[@]}"; do
  name="$(basename "$seq" .seq)"
  i=$((i + 1))
  job="$JOB_DIR/job_$i.sh"
  {
    echo "#!/usr/bin/env bash"
    echo "set -u"
    printf '%q ' "$RUN_SEQUENCE" "$seq" --prebuilt-build "$SHARED_BUILD_DIR"
    for a in "${PASSTHROUGH[@]+"${PASSTHROUGH[@]}"}"; do printf '%q ' "$a"; done
    echo
    echo "echo \$? > '$JOB_DIR/result_$name'"
  } > "$job"
  chmod +x "$job"
done

echo "Running ${#SEQS[@]} sequences across $JOBS parallel containers..."
printf '%s\n' "$JOB_DIR"/job_*.sh | xargs -P "$JOBS" -I{} bash -c '{} 2>&1 | sed "s/^/[$(basename {} .sh)] /"'

PASS=0
FAIL=0
FAILED_NAMES=()
for seq in "${SEQS[@]}"; do
  name="$(basename "$seq" .seq)"
  result_file="$JOB_DIR/result_$name"
  if [ -f "$result_file" ] && [ "$(cat "$result_file")" = "0" ]; then
    PASS=$((PASS + 1))
  else
    FAIL=$((FAIL + 1))
    FAILED_NAMES+=("$name")
  fi
done

echo
echo "========================================"
echo "$PASS passed, $FAIL failed (of ${#SEQS[@]} run, pattern '$PATTERN', $JOBS parallel)"
if [ "$FAIL" -gt 0 ]; then
  echo "Failed:"
  printf '  - %s\n' "${FAILED_NAMES[@]}"
  exit 1
fi
exit 0
