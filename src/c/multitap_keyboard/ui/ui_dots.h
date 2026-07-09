#pragma once
#include <pebble.h>

// =============================================================================
// ui_dots — pagination dots. Draws `count` evenly spaced dots centered in `box`,
// the `active` one filled with the accent and slightly larger; the rest a
// smaller muted dot. Stateless, like the other lib renderers: the caller owns
// the page index and calls this each draw. A no-op for count <= 1 (one page
// needs no indicator).
//
// Two styles:
//   UI_DOTS_FOOTER   — bare dots, for a pinned band that has its own divider.
//   UI_DOTS_FLOATING — the dots inside a bordered, rounded "island" pill that
//                      floats over the content, "( o o o o )".
// =============================================================================

typedef enum {
  UI_DOTS_FOOTER,
  UI_DOTS_FLOATING,
} UiDotsStyle;

void ui_dots_draw(GContext *ctx, GRect box, int count, int active, UiDotsStyle style);
