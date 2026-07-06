// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <pebble.h>

typedef void (*DialTouchSelectionCallback)(uint8_t hours, uint8_t minutes, uint8_t seconds);

void dial_touch_create(Layer *parent, DialTouchSelectionCallback callback);
void dial_touch_destroy(void);
void dial_touch_enable(bool enable);
bool dial_touch_in_progress(void);
