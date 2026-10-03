#!/usr/bin/env bash
# Runs every functional-test sequence under a project's own sequences
# directory, across several parallel runs AT ONCE - the generic "does the
# whole suite pass" / CI / golden-approval entrypoint. App-agnostic, like
# the rest of this framework - everything app-specific comes from the
# conf file passed via --conf.
#
# In container mode (see --conf's own CONTAINER key, or --container/
# --no-container below), each sequence gets its own fully isolated
# container (its own private source copy, its own qemu-pebble/pypkjs pair
# on isolated container-internal ports) instead of sharing one
# host-managed emulator instance, so there's no shared state between
# parallel jobs to race on - see container/README.md. Native mode has no
# such isolation: running several sequences against ONE shared host
# emulator concurrently is unsafe, so a native batch run here is only as
# safe as whatever emulator-sharing strategy the caller has arranged
# (usually: don't run -j greater than 1 natively).
#
# A native run's screenshots also aren't reproducible run to run (the
# emulator's displayed clock/elapsed counters depend on real wall-clock
# time with no pinning unless the app itself freezes its own display), so
# update_golden() (lib/golden.sh) refuses to approve a golden baseline
# from anything but a containerized run regardless of what this script
# does - see that file's own comment.
#
# Usage:
#   run_batch.sh --conf <app.conf> [--seq-dir DIR] [-j N] [--pattern GLOB]
#              [--out-dir DIR] [--golden-dir DIR] [--update-golden]
#              [--continue-on-error] [--container] [--no-container]
#
# Flags:
#   --conf FILE             App config file - see run_sequence.sh/README.md
#                           for the full key reference.
#   --seq-dir DIR           Directory of .seq files to run. Default: the
#                           conf file's own SEQ_DIR key, resolved relative
#                           to the conf file's directory (default within
#                           that: "sequences/walkthroughs").
#   -j N                    How many sequences run at once. Default: 2.
#                           A qemu-pebble instance itself is lightweight,
#                           so in container mode this is bounded by host
#                           CPU/memory for N concurrent containers, not by
#                           the emulator - raise it if the host has room
#                           (see container/README.md's Memory section).
#                           Several real-wall-clock-timing-sensitive
#                           sequences can also make too-high parallelism
#                           starve a job badly enough to produce a
#                           different, unrecoverable screen state rather
#                           than a mere timing miss - lower this if that's
#                           suspected.
#   --pattern GLOB          Restrict which sequences run, matched against
#                           each .seq file's basename (e.g.
#                           'wakeup_conflict_*' for just that family).
#                           Default: '*' (everything).
#   --out-dir DIR           Forwarded to each sequence's own run_sequence.sh
#                           invocation (see its own --out-dir).
#   --golden-dir DIR        Forwarded to each sequence's own run_sequence.sh
#                           invocation (see its own --golden-dir). If not
#                           given, each sequence falls back to its own
#                           conf-file default independently, same as a
#                           standalone run_sequence.sh invocation would.
#   --update-golden         Forwarded to each sequence's own run_sequence.sh
#                           invocation.
#   --continue-on-error     Forwarded to each sequence's own run_sequence.sh
#                           invocation, so a failed STEP within one
#                           sequence doesn't stop that sequence early
#                           either. This script itself always runs every
#                           matching sequence regardless of this flag - see
#                           "Failure behavior" below.
#   --container/--no-container  Override the conf file's CONTAINER default
#                           for the whole batch, same as run_sequence.sh's
#                           own flags of the same name.
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
RUN_SEQUENCE="$SCRIPT_DIR/run_sequence.sh"

usage() {
  sed -n '2,70p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

CONF_FILE=""
SEQ_DIR_OVERRIDE=""
JOBS=2
PATTERN='*'
OUT_DIR_OVERRIDE=""
GOLDEN_DIR_OVERRIDE=""
UPDATE_GOLDEN=0
CONTINUE_ON_ERROR=0
CONTAINER_OVERRIDE=""

while [ $# -gt 0 ]; do
  case "$1" in
    --conf) CONF_FILE="$2"; shift 2 ;;
    --seq-dir) SEQ_DIR_OVERRIDE="$2"; shift 2 ;;
    -j) JOBS="$2"; shift 2 ;;
    --pattern) PATTERN="$2"; shift 2 ;;
    --out-dir) OUT_DIR_OVERRIDE="$2"; shift 2 ;;
    --golden-dir) GOLDEN_DIR_OVERRIDE="$2"; shift 2 ;;
    --update-golden) UPDATE_GOLDEN=1; shift ;;
    --continue-on-error) CONTINUE_ON_ERROR=1; shift ;;
    --container) CONTAINER_OVERRIDE="1"; shift ;;
    --no-container) CONTAINER_OVERRIDE="0"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ -z "$CONF_FILE" ] || [ ! -f "$CONF_FILE" ]; then
  echo "--conf <app.conf> is required and must exist." >&2
  usage >&2
  exit 1
fi
CONF_FILE="$(cd "$(dirname "$CONF_FILE")" && pwd)/$(basename "$CONF_FILE")"
CONF_DIR="$(cd "$(dirname "$CONF_FILE")" && pwd)"

# Read just enough of the conf file ourselves (SEQ_DIR/CONTAINER) to decide
# the default sequences directory and whether to use the container
# shared-build-once optimization below - run_sequence.sh re-sources the
# same conf file independently per sequence, so this isn't the only place
# that matters, just the one place THIS script needs it for.
# shellcheck source=/dev/null
source "$CONF_FILE"
SEQ_DIR="${SEQ_DIR_OVERRIDE:-${SEQ_DIR:-sequences/walkthroughs}}"
case "$SEQ_DIR" in
  /*) : ;;
  *) SEQ_DIR="$CONF_DIR/$SEQ_DIR" ;;
esac
CONTAINER="${CONTAINER_OVERRIDE:-${CONTAINER:-0}}"

PASSTHROUGH=()
[ -n "$OUT_DIR_OVERRIDE" ] && PASSTHROUGH+=(--out-dir "$OUT_DIR_OVERRIDE")
[ -n "$GOLDEN_DIR_OVERRIDE" ] && PASSTHROUGH+=(--golden-dir "$GOLDEN_DIR_OVERRIDE")
[ "$UPDATE_GOLDEN" = "1" ] && PASSTHROUGH+=(--update-golden)
[ "$CONTINUE_ON_ERROR" = "1" ] && PASSTHROUGH+=(--continue-on-error)
[ -n "$CONTAINER_OVERRIDE" ] && { [ "$CONTAINER_OVERRIDE" = "1" ] && PASSTHROUGH+=(--container) || PASSTHROUGH+=(--no-container); }

SEQS=("$SEQ_DIR"/$PATTERN.seq)
if [ ! -e "${SEQS[0]}" ]; then
  echo "No sequences matched pattern '$PATTERN' in $SEQ_DIR" >&2
  exit 1
fi

JOB_DIR="$(mktemp -d)"
trap 'rm -rf "$JOB_DIR"' EXIT

# One shared timestamp for the whole batch, generated HERE on the host
# (never touched by any container's own libfaketime) and forwarded to
# every job below as --run-id, so the whole batch's output lands under one
# shared, browsable out-dir instead of scattering across a separate
# timestamp per sequence (see run_sequence.sh's own --run-id and
# container/entrypoint.sh's RUN_ID_OVERRIDE handling for the rest of this
# mechanism).
RUN_ID="$(date '+%Y%m%d_%H%M%S')"
PASSTHROUGH+=(--run-id "$RUN_ID")
echo "Batch run id: $RUN_ID"

if [ "$CONTAINER" = "1" ]; then
  # Build ONCE for this whole batch (inside the same image every sequence
  # container uses, so identical SDK/toolchain/build-env - no
  # version-drift risk) instead of once per sequence container - every one
  # of them would otherwise independently npm-install + pebble-build the
  # exact same source under the exact same BUILD_ENV, producing identical
  # output regardless of which container does it. A plain mktemp scratch
  # dir, not tied to this framework's own location in a project's tree -
  # cleaned up on exit alongside JOB_DIR.
  SHARED_BUILD_DIR="$(mktemp -d)"
  trap 'rm -rf "$JOB_DIR" "$SHARED_BUILD_DIR"' EXIT
  echo "Building once (shared across all $JOBS parallel containers)..."
  if ! "$RUN_SEQUENCE" --conf "$CONF_FILE" --build-only "$SHARED_BUILD_DIR"; then
    echo "Shared build failed - aborting batch." >&2
    exit 1
  fi
  PASSTHROUGH+=(--prebuilt-build "$SHARED_BUILD_DIR")
fi

# One small job script per sequence (rather than trying to pass an array
# of PASSTHROUGH args through xargs -I{} directly, which mangles
# multi-word arguments) - each writes its own pass/fail marker file, since
# xargs -P workers' exit codes aren't otherwise easy to collect back
# reliably.
i=0
for seq in "${SEQS[@]}"; do
  name="$(basename "$seq" .seq)"
  i=$((i + 1))
  job="$JOB_DIR/job_$i.sh"
  {
    echo "#!/usr/bin/env bash"
    echo "set -u"
    printf '%q ' "$RUN_SEQUENCE" --conf "$CONF_FILE" --seq "$seq"
    for a in "${PASSTHROUGH[@]+"${PASSTHROUGH[@]}"}"; do printf '%q ' "$a"; done
    echo
    echo "echo \$? > '$JOB_DIR/result_$name'"
  } > "$job"
  chmod +x "$job"
done

echo "Running ${#SEQS[@]} sequences, $JOBS at a time..."
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
