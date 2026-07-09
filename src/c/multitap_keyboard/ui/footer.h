#pragma once
#include <pebble.h>

// =============================================================================
// footer — a status bar pinned to the bottom of a window. A left text region
// (an optional label plus an optional live clock that follows the watch's 12/24h
// setting) and an optional right-side *focusable action* (an icon the host arms
// with key presses, not touch). The footer is a passive Layer: it draws what the
// host puts in its model and refreshes the clock once a minute on its own. The
// host owns focus and routes Select to the action.
//
// Reserve FOOTER_H at the bottom of the window for it; size the scroll/content
// area above to (window height - FOOTER_H).
// =============================================================================

#define FOOTER_H 28

typedef struct {
  char     left[24];        // left label, e.g. "Round 3" (may be empty)
  bool     show_clock;      // append the watch-formatted time after the label
  uint32_t action_icon;     // right action icon resource (0 = no action)
  bool     action_enabled;  // false → drawn muted; cannot be armed
  bool     action_focused;  // true → armed: highlighted with the accent
} FooterModel;

typedef struct Footer Footer;

// Create a footer pinned to the bottom of `parent` (a window's root layer) and
// add it as a child. Subscribes to the minute tick for the clock.
Footer *footer_create(Layer *parent);
void    footer_set_model(Footer *f, FooterModel model);
void    footer_destroy(Footer *f);
