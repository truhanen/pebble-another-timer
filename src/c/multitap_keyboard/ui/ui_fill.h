#pragma once
#include <pebble.h>

// =============================================================================
// ui_fill — fills that graphics_fill_rect can't express on the 64-color display.
//
// ui_fill_dither stipples `color` over `rect` on a checkerboard, so the eye
// averages it with whatever's underneath. That's the lib's way to fake a tone
// the hardware has no solid for — e.g. a gray *lighter* than light-gray, by
// dithering light-gray over a white base. Stateless: paint the base first, then
// call this to dither a color on top.
// =============================================================================

void ui_fill_dither(GContext *ctx, GRect rect, GColor color);
