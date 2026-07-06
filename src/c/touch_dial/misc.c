// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// Miscellaneous standalone generic functions

#include "misc.h"

#include <math.h>

#include "macros.h"


// Return milliseconds since the epoch
uint32_t timestamp_ms(void) {
    time_t seconds = 0;
    const uint32_t millis = (uint32_t)time_ms(&seconds, NULL);
    return ((uint32_t)seconds * MS_PER_S) + millis;
}

// Fill a circle with color
void graphics_color_circle(GContext* ctx, GPoint p, uint16_t radius, GColor color){
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, p, radius);
}

// TODO Fill a radial with color
// static inline void graphics_color_circle(GContext* ctx, GRect bounds, GColor color){
//     graphics_context_set_fill_color(ctx, color);
//     const GRect bounds = {
//         .origin = {0, 0}
//         .size = {}
//     }
//     graphics_fill_circle(ctx, p, radius);
//     graphics_fill_radial(ctx, bounds, GOvalScaleModeFillCircle, ring_thickness,
//                          DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(360));
// }


// Return a GPoint that is `distance` away from `origin` at `angle`.
// If `origin` is 0, this is equivalent to converting `angle` to a cartesian vector of magnitude `distance`.
GPoint point_from_angle(GPoint origin, int32_t angle, int32_t distance) {
    return (GPoint) {
        .x = (int16_t)((sin_lookup(angle) * distance) / TRIG_MAX_RATIO) + origin.x,
        .y = (int16_t)((-cos_lookup(angle) * distance) / TRIG_MAX_RATIO) + origin.y
    };
}

// quake 3 sqrt
float fast_sqrt(const float x) {
    const float xhalf = 0.5f * x;
    union {
        float x;
        int i;
    } u;
    u.x = x;
    u.i = 0x5f3759df - (u.i >> 1);  // initial guess
    return x * u.x * (1.5f - xhalf * u.x * u.x);  // Newton step
}

/// Return the diagonal length of `rect`
int32_t grect_diagonal(GRect rect) {
    return ceil(fast_sqrt(
        (rect.size.h * rect.size.h)
        + (rect.size.w * rect.size.w)
    ));
}

// Return the time format for strftime
const char* time_fmt(void) {
    return clock_is_24h_style() ? "%H:%M" : "%I:%M %p";
}


// format a time_t into a string
void snprintf_time(char* target, size_t size, const char* fmt, time_t time) {
    char time_str[MAX_TIME_TEXT_SIZE] = {0};
    strftime(time_str, sizeof(time_str), time_fmt(), localtime(&time));
    snprintf(target, size, fmt, time_str);
}

// Given a GRect that is the entire root window frame,
// return a GRect shrunk for the status and action bars.
GRect reduce_frame_for_system_bars(const GRect frame) {
#if PBL_ROUND
    return frame;
#else // PBL_RECT
    return (GRect) {
        .origin = {
            .x = frame.origin.x,
            .y = frame.origin.y + STATUS_BAR_LAYER_HEIGHT
        },
        .size = {
            .w = frame.size.w - ACTION_BAR_WIDTH,
            .h = frame.size.h - STATUS_BAR_LAYER_HEIGHT
        }
    };
#endif // PBL_RECT
}

// set the value at `index` in `bitmap`'s palette to `color`
void gbitmap_set_color(GBitmap* bitmap, size_t index, GColor color) {
    gbitmap_get_palette(bitmap)[index] = color;
}
