#pragma once
#include <pebble.h>
#include "ui_dots.h"

// =============================================================================
// pager — a multi-page scrollable screen with a pagination-dots footer.
//
// Each page is opaque content: a draw proc plus a content height (or a measure
// callback when the height depends on width, e.g. wrapped text). The pager owns
// the scroll, the dots band, and a scroll_hint overlay, and maps the buttons:
//
//   Down  — scroll down; when already at the bottom, go to the next page.
//   Up    — scroll up;   when already at the top, go to the previous page.
//   Back  — pop the window.
//
// Up/Down are repeating clicks (hold to scroll at a constant speed; tap to nudge),
// so a held button reads as one continuous scroll across the whole document.
// Turning forward lands at the next page's top; turning back lands at the previous
// page's bottom, keeping an upward scroll continuous. The down/up affordance
// bubbles (scroll_hint) show whenever there's more to scroll OR a page to turn to
// in that direction; the dots footer shows which page you're on.
//
// This is distinct from paged_list (which pages a MenuLayer's data, e.g. history
// loaded from the phone) — pager is a wizard-style fixed set of content pages.
// =============================================================================

// Draws the page's content into a layer whose bounds are (page width × at least
// the viewport height); the page lays out from the top. `data` is the page's.
typedef void (*PageDraw)(GContext *ctx, GRect bounds, void *data);

// Optional: the page's content height for a given width (for wrapped text).
// When NULL, the fixed `height` is used.
typedef int16_t (*PageMeasure)(int width, void *data);

typedef struct {
  PageDraw    draw;
  PageMeasure measure;   // NULL → use `height`
  int16_t     height;    // content height when measure is NULL
  void       *data;      // passed back to draw / measure
} Page;

typedef struct Pager Pager;

// Copies `pages` and pushes the screen; frees itself when popped. `dots` picks
// the page-indicator style: UI_DOTS_FOOTER (divider + bare dots, pinned to the
// bottom) or UI_DOTS_FLOATING (no divider, dots framed as "( o o o )").
Pager *pager_push(const Page *pages, int n, UiDotsStyle dots);
