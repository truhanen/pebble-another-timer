#!/usr/bin/env bash
# Runs every functional-test sequence under sequences/walkthroughs/ across
# several podman containers AT ONCE (see run_container.sh/Containerfile),
# instead of one after another against a single shared emulator like
# tests/functional/run_all.sh does. Each container gets its own private
# copy of the source (see run-sequence-in-container.sh) and its own
# qemu-pebble/pypkjs pair on isolated container-internal ports, so there's
# no shared state between parallel jobs to race on.
#
# Usage:
#   tests/functional/docker/run_all_parallel.sh [-j N] [--pattern GLOB]
#                                                [--golden-dir DIR] [--fuzz PERCENT]
#                                                [--mask-rect WxH+X+Y] [--update-golden]
#                                                [--continue-on-error]
#
# -j N sets how many containers run at once (default 4 - a qemu-pebble
# instance is lightweight (Cortex-M33 TCG emulation), so this is mostly
# bounded by host CPU/memory for N concurrent Rosetta-translated x86_64
# containers, not by the emulator itself; raise it if the host has room).
# --pattern restricts which sequences run, matched against each .seq
# file's basename glob, same meaning as run_all.sh's own --pattern.
# The image must already be built - see Containerfile's own header.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SEQ_DIR="$REPO_ROOT/tests/functional/sequences/walkthroughs"
RUN_CONTAINER="$SCRIPT_DIR/run_container.sh"

usage() {
  sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'
}

JOBS=4
PATTERN='*'
PASSTHROUGH=()

while [ $# -gt 0 ]; do
  case "$1" in
    -j) JOBS="$2"; shift 2 ;;
    --pattern) PATTERN="$2"; shift 2 ;;
    --golden-dir) PASSTHROUGH+=(--golden-dir "$2"); shift 2 ;;
    --update-golden) PASSTHROUGH+=(--update-golden); shift ;;
    --fuzz) PASSTHROUGH+=(--fuzz "$2"); shift 2 ;;
    --mask-rect) PASSTHROUGH+=(--mask-rect "$2"); shift 2 ;;
    --continue-on-error) PASSTHROUGH+=(--continue-on-error); shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

SEQS=("$SEQ_DIR"/$PATTERN.seq)
if [ ! -e "${SEQS[0]}" ]; then
  echo "No sequences matched pattern '$PATTERN' in $SEQ_DIR" >&2
  exit 1
fi

JOB_DIR="$(mktemp -d)"
trap 'rm -rf "$JOB_DIR"' EXIT

# One shared timestamp for the whole batch, generated HERE on the host
# (never touched by any container's own libfaketime) and exported so every
# job script below inherits it, which run_container.sh then forwards into
# its container as RUN_ID_OVERRIDE - see run-sequence-in-container.sh's
# own comment for the full mechanism. Without this, each container
# generates its own independent RUN_ID and the batch's output scatters
# across dozens of unrelated top-level out/ directories instead of landing
# under one shared, browsable one - the same reason native run_all.sh's
# BATCH_RUN_ID exists.
export RUN_ID_OVERRIDE="$(date '+%Y%m%d_%H%M%S')"
echo "Batch output directory: $REPO_ROOT/tests/functional/out/$RUN_ID_OVERRIDE/"

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
    printf '%q ' "$RUN_CONTAINER" "$seq"
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
