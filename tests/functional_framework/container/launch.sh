#!/usr/bin/env bash
# Host-side podman launcher for the functional_framework test-runner image.
# Invoked only by run_sequence.sh's own container-dispatch branch (see its
# header comment) - not meant for direct end-user use, so its flags are an
# internal contract between the two scripts rather than a documented
# public interface. App-agnostic: everything it needs comes from argv,
# never hardcoded.
#
# Usage (single sequence):
#   launch.sh --repo-root DIR --framework-rel RELPATH --image-tag TAG \
#     --conf-rel RELPATH --seq-rel RELPATH --out-dir DIR \
#     [--golden-dir DIR] [--run-id ID] [--touch] [--prebuilt-build DIR] \
#     [--build-env STR] -- [extra flags forwarded to the in-container
#     run_sequence.sh invocation, e.g. --fuzz/--mask-rect/
#     --continue-on-error/--platform/--vnc/--no-vnc]
#
# Usage (shared build, for run_batch.sh's batch mode):
#   launch.sh --repo-root DIR --framework-rel RELPATH --image-tag TAG \
#     --build-only DIR [--build-env STR]
#
# --init is required for container mode (see Containerfile's own comment
# on why - a real init process as PID 1 is what lets pebble-tool's own
# kill/wipe/install cycle work correctly across more than one call in the
# same container).
set -eu

REPO_ROOT=""
FRAMEWORK_REL=""
IMAGE_TAG=""
BUILD_ENV=""
BUILD_ONLY_DIR=""
CONF_REL=""
SEQ_REL=""
OUT_DIR=""
GOLDEN_DIR=""
RUN_ID=""
TOUCH_REQUESTED=0
PREBUILT_BUILD_DIR=""
FORWARD_ARGS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --repo-root) REPO_ROOT="$2"; shift 2 ;;
    --framework-rel) FRAMEWORK_REL="$2"; shift 2 ;;
    --image-tag) IMAGE_TAG="$2"; shift 2 ;;
    --build-env) BUILD_ENV="$2"; shift 2 ;;
    --build-only) BUILD_ONLY_DIR="$2"; shift 2 ;;
    --conf-rel) CONF_REL="$2"; shift 2 ;;
    --seq-rel) SEQ_REL="$2"; shift 2 ;;
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    --golden-dir) GOLDEN_DIR="$2"; shift 2 ;;
    --run-id) RUN_ID="$2"; shift 2 ;;
    --touch) TOUCH_REQUESTED=1; shift ;;
    --prebuilt-build) PREBUILT_BUILD_DIR="$2"; shift 2 ;;
    --) shift; FORWARD_ARGS=("$@"); break ;;
    *) echo "launch.sh: unknown argument: $1" >&2; exit 1 ;;
  esac
done

: "${REPO_ROOT:?launch.sh: --repo-root is required}"
: "${FRAMEWORK_REL:?launch.sh: --framework-rel is required}"
: "${IMAGE_TAG:?launch.sh: --image-tag is required}"

BUILD_ENV_ENV=()
[ -n "$BUILD_ENV" ] && BUILD_ENV_ENV=(-e "BUILD_ENV=$BUILD_ENV")

if [ -n "$BUILD_ONLY_DIR" ]; then
  mkdir -p "$BUILD_ONLY_DIR"
  OUT_BUILD_DIR="$(cd "$BUILD_ONLY_DIR" && pwd)"
  exec podman run --rm --init --platform linux/amd64 \
    -v "$REPO_ROOT":/src:ro \
    -v "$OUT_BUILD_DIR":/out-build \
    "${BUILD_ENV_ENV[@]+"${BUILD_ENV_ENV[@]}"}" \
    "$IMAGE_TAG" --build-only
fi

: "${CONF_REL:?launch.sh: --conf-rel is required}"
: "${SEQ_REL:?launch.sh: --seq-rel is required}"
: "${OUT_DIR:?launch.sh: --out-dir is required}"

mkdir -p "$OUT_DIR"
OUT_HOST="$(cd "$OUT_DIR" && pwd)"

GOLDEN_MOUNT=()
if [ -n "$GOLDEN_DIR" ]; then
  # On macOS, this host path MUST be somewhere Podman's own VM actually
  # shares (in practice, somewhere under $HOME) - a bare /tmp/... path
  # fails at podman-run time with `Error: statfs ...: no such file or
  # directory`, since /tmp isn't in the applehv machine's default
  # shared-mount scope. Not something this script can detect or fix in
  # advance - see container/README.md.
  mkdir -p "$GOLDEN_DIR"
  GOLDEN_HOST="$(cd "$GOLDEN_DIR" && pwd)"
  GOLDEN_MOUNT=(-v "$GOLDEN_HOST:/golden")
  FORWARD_ARGS+=(--golden-dir /golden)
fi

PREBUILT_MOUNT=()
PREBUILT_ENV=()
if [ -n "$PREBUILT_BUILD_DIR" ]; then
  PREBUILT_HOST="$(cd "$PREBUILT_BUILD_DIR/build" && pwd)"
  PREBUILT_MOUNT=(-v "$PREBUILT_HOST:/prebuilt-build:ro")
  PREBUILT_ENV=(-e "PREBUILT_BUILD=1")
fi

TOUCH_ENV=()
[ "$TOUCH_REQUESTED" = "1" ] && TOUCH_ENV=(-e "PEBBLE_TEST_TOUCH=1")

RUN_ID_ENV=()
[ -n "$RUN_ID" ] && RUN_ID_ENV=(-e "RUN_ID_OVERRIDE=$RUN_ID")

podman run --rm --init --platform linux/amd64 \
  -v "$REPO_ROOT":/src:ro \
  -v "$OUT_HOST":/out \
  "${GOLDEN_MOUNT[@]+"${GOLDEN_MOUNT[@]}"}" \
  "${PREBUILT_MOUNT[@]+"${PREBUILT_MOUNT[@]}"}" \
  "${PREBUILT_ENV[@]+"${PREBUILT_ENV[@]}"}" \
  "${TOUCH_ENV[@]+"${TOUCH_ENV[@]}"}" \
  "${RUN_ID_ENV[@]+"${RUN_ID_ENV[@]}"}" \
  "${BUILD_ENV_ENV[@]+"${BUILD_ENV_ENV[@]}"}" \
  -e "FRAMEWORK_REL=$FRAMEWORK_REL" \
  "$IMAGE_TAG" "$CONF_REL" "$SEQ_REL" \
  "${FORWARD_ARGS[@]+"${FORWARD_ARGS[@]}"}"
