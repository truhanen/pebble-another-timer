#!/bin/bash
# Rasterize an SVG into the icon sizes Pebble app resources need (25x25,
# 80x80, 144x144), then quantize the result onto Pebble's platform palette:
# RGB is remapped onto the 64-color palette (scripts/pebble_colors_64.gif,
# a copy of the indexed reference image from the Pebble iconography repo —
# see journal.md's "2025-11-30 App icon" entry) and alpha is posterized to
# Pebble's 2-bit (4-level) alpha channel. Requires ImageMagick (`magick`).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PALETTE="$SCRIPT_DIR/pebble_colors_64.gif"

SVG="${1:-resources/icon.svg}"

if [ ! -f "$SVG" ]; then
  echo "SVG file not found: $SVG" >&2
  exit 1
fi

OUTDIR="$(dirname "$SVG")"
MENU_ICON_OUT="resources/images/menuicon.png"

for size in 25 80 144; do
  if [ "$size" -eq 25 ]; then
    OUT="$MENU_ICON_OUT"
  else
    OUT="$OUTDIR/icon_${size}x${size}.png"
  fi
  RAW="$(mktemp -t icon_raw).png"
  MASK="$(mktemp -t icon_mask).png"
  RGB="$(mktemp -t icon_rgb).png"

  sips --resampleHeightWidth "$size" "$size" -s format png "$SVG" -o "$RAW" >/dev/null

  if [ "$size" -ne 25 ]; then
    # The 80x80/144x144 icons render the symbol with noticeably tighter
    # top/bottom margin than left/right (e.g. at 144px: ~6px top/bottom vs.
    # ~24px left/right) since the SVG's own viewBox isn't square-symmetric
    # around the glyph. Shrink slightly and re-center on the full canvas to
    # add a bit more even margin on all sides (most visible top/bottom,
    # where it was tightest) before quantizing.
    scaled=$(( size * 95 / 100 ))
    magick "$RAW" -resize "${scaled}x${scaled}" -background none \
      -gravity center -extent "${size}x${size}" "$RAW"
  fi

  # Pebble's alpha channel is 2-bit (4 levels: 0/85/170/255). Rasterizing at
  # non-integer scale factors (e.g. a 24-unit viewBox to 80px) leaves faint,
  # near-invisible anti-aliasing bleed (alpha ~10-30) far from any real edge;
  # posterizing that directly rounds it UP to the nearest level (e.g. 85),
  # turning invisible noise into visible stray gray flecks. Clamp anything
  # below 15% alpha to fully transparent first so only real edge coverage
  # survives to be posterized.
  magick "$RAW" -channel A -level 15%,100% -posterize 4 +channel -alpha extract "$MASK"
  # Remap RGB onto the 64-color platform palette, ignoring alpha for the match.
  magick "$RAW" -alpha off -dither None -remap "$PALETTE" "$RGB"
  # Recombine the quantized RGB with the quantized alpha mask.
  if [ "$size" -eq 25 ]; then
    magick "$RGB" "$MASK" -alpha off -compose CopyOpacity -composite "$OUT"
  else
    # The 80x80/144x144 app-store icons must have an opaque white
    # background (transparency there would show through as black on
    # some store surfaces) — flatten the quantized+masked composite onto
    # white and drop the alpha channel entirely.
    magick "$RGB" "$MASK" -alpha off -compose CopyOpacity -composite \
      -background white -alpha remove -alpha off "$OUT"
  fi

  rm -f "$RAW" "$MASK" "$RGB"
  echo "Wrote $OUT"
done
