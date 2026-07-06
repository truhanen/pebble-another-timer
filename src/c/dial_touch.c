// SPDX-License-Identifier: GPL-3.0-only

#include "dial_touch.h"
#include "touch_dial/touch.h"

#if PBL_TOUCH

static DialTouchSelectionCallback s_callback;

static void on_touch_selection(bool is_duration, uint8_t hours, uint8_t minutes, uint8_t seconds) {
  (void)is_duration;
  if (s_callback) { s_callback(hours, minutes, seconds); }
}

static void on_touch_event(const TouchEvent *event, void *context) {
  (void)event;
  (void)context;
}

void dial_touch_create(Layer *parent, DialTouchSelectionCallback callback) {
  s_callback = callback;
  touch_create(parent, on_touch_selection, on_touch_event);
}

void dial_touch_destroy(void) {
  touch_destroy();
  s_callback = NULL;
}

void dial_touch_enable(bool enable) {
  touch_enable(enable);
}

bool dial_touch_in_progress(void) {
  return touch_in_progress();
}

#else

void dial_touch_create(Layer *parent, DialTouchSelectionCallback callback) {
  (void)parent;
  (void)callback;
}

void dial_touch_destroy(void) {}

void dial_touch_enable(bool enable) {
  (void)enable;
}

bool dial_touch_in_progress(void) {
  return false;
}

#endif
