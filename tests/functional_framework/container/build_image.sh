#!/usr/bin/env bash
# Builds (or rebuilds) the functional_framework test-runner image for a
# project, reading IMAGE_TAG/SDK_VERSION from its own app.conf instead of
# requiring either to be typed out by hand on every rebuild. App-agnostic.
#
# Usage:
#   build_image.sh --conf <app.conf>
#
# Rebuild whenever container/Containerfile, container/entrypoint.sh, or
# the conf file's own SDK_VERSION/IMAGE_TAG changes; you do NOT need to
# rebuild when the app's own source changes - run_sequence.sh/launch.sh
# mount the repo fresh into every container run (see Containerfile's own
# comments for why: a build baked into the image would go stale
# immediately, and copying at container-start time is what makes parallel
# runs safe against each other in the first place).
set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

CONF_FILE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --conf) CONF_FILE="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ -z "$CONF_FILE" ] || [ ! -f "$CONF_FILE" ]; then
  echo "--conf <app.conf> is required and must exist." >&2
  usage >&2
  exit 1
fi

# shellcheck source=/dev/null
source "$CONF_FILE"
: "${APP_UUID:?APP_UUID must be set in $CONF_FILE}"
: "${SDK_VERSION:?SDK_VERSION must be set in $CONF_FILE to build the container image}"
IMAGE_TAG="${IMAGE_TAG:-pebble-functional-tests-$APP_UUID}"

echo "Building $IMAGE_TAG (SDK_VERSION=$SDK_VERSION)..."
podman build --platform linux/amd64 \
  --build-arg SDK_VERSION="$SDK_VERSION" \
  -t "$IMAGE_TAG" \
  -f "$SCRIPT_DIR/Containerfile" \
  "$SCRIPT_DIR"
