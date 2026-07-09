#pragma once
#include <pebble.h>

// Place fixed-size content inside a box. Text centering lives in ui_text.

typedef enum { UI_ALIGN_START, UI_ALIGN_CENTER, UI_ALIGN_END } UiAlign;

// Position `content` inside `box` by horizontal/vertical alignment.
GRect ui_rect_align(GRect box, GSize content, UiAlign h, UiAlign v);

// Shorthand for the common case: centered on both axes.
static inline GRect ui_rect_center(GRect box, GSize content) {
  return ui_rect_align(box, content, UI_ALIGN_CENTER, UI_ALIGN_CENTER);
}
