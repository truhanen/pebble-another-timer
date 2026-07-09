#pragma once
#include <pebble.h>

// Corner scroll/page hints. Each visible bubble nudges briefly, then rests.
// `frame` is the content area the bubbles tuck into.

typedef struct {
  bool down;   // show the bottom-right down bubble
  bool up;     // show the top-right up bubble
} ScrollHintModel;

typedef struct ScrollHint ScrollHint;

ScrollHint *scroll_hint_create(Layer *parent, GRect frame);
void        scroll_hint_set(ScrollHint *h, ScrollHintModel model);
void        scroll_hint_destroy(ScrollHint *h);
