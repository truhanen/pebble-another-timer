// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include <pebble.h>


#define MAX_TIME_TEXT_SIZE (50)

// Is the backlight service available
#define PBL_BACKLIGHT_SERVICE (!(PBL_PLATFORM_APLITE || PBL_PLATFORM_BASALT \
                                 || PBL_PLATFORM_CHALK || PBL_PLATFORM_DIORITE))


uint32_t timestamp_ms(void);
void graphics_color_circle(GContext* ctx, GPoint p, uint16_t radius, GColor color);
GPoint point_from_angle(GPoint origin, int32_t angle, int32_t distance);
float fast_sqrt(const float x);
int32_t grect_diagonal(GRect rect);
const char* time_fmt(void);
void snprintf_time(char* target, size_t size, const char* fmt, time_t time);
GRect reduce_frame_for_system_bars(const GRect frame);
void gbitmap_set_color(GBitmap* bitmap, size_t index, GColor color);
