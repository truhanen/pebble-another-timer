#!/usr/bin/env bash
# Generates reference screenshots of the app's main views, via
# tests/functional_framework's containerized, TOUCH-capable emulator
# driving (see scripts/create_screenshots.seq for the actual walkthrough).
# Fully scripted - unlike the old version of this script, nothing here
# needs a human at the keyboard/mouse for the touch dial or the on-screen
# label keyboard.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$REPO_ROOT/screenshots"
RUN_BASE="$REPO_ROOT/scripts/create_screenshots_out"

usage() {
  cat <<EOF
Usage: $(basename "$0") [-o OUT_DIR]

  -o, --output   Directory to write screenshots into. Default: $OUT_DIR
  -h, --help     Show this help.

Run output (run.log, intermediate screenshots) is kept under
$RUN_BASE for inspection after this script exits - safe to delete any
time, it's gitignored and rebuilt fresh on every run.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -o|--output) OUT_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

rm -rf "$RUN_BASE"
mkdir -p "$OUT_DIR" "$RUN_BASE"

"$REPO_ROOT/tests/functional_framework/run_sequence.sh" \
  --conf "$REPO_ROOT/scripts/create_screenshots.conf" \
  --seq "$REPO_ROOT/scripts/create_screenshots.seq" \
  --out-dir "$RUN_BASE"

# run_sequence.sh always nests output as <out-dir>/<RUN_ID>/<sequence-name>/
# - there's exactly one such directory here since RUN_BASE is wiped above.
RUN_DIR="$(find "$RUN_BASE" -mindepth 2 -maxdepth 2 -type d -name create_screenshots)"
if [ -z "$RUN_DIR" ]; then
  echo "Could not find run output under $RUN_BASE" >&2
  exit 1
fi

cp "$RUN_DIR"/*.png "$OUT_DIR"/
echo "Screenshots copied to $OUT_DIR"
echo "Full run log kept at $RUN_DIR/run.log"
