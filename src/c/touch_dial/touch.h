// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include <pebble.h>

#if PBL_TOUCH


typedef void (*TouchSelectionCallback)(bool is_duration, uint8_t hours, uint8_t minutes, uint8_t seconds);

void touch_create(Layer* parent, TouchSelectionCallback callback, TouchServiceHandler handler);
void touch_destroy(void);
void touch_enable(bool enable);
bool touch_in_progress(void);


#endif // PBL_TOUCH
