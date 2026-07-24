// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// A radial touch selection layer


#define DEBUG 0  // TODO disable

#include "touch.h"

#include <pebble.h>

#if PBL_TOUCH

#include "config.h"
#include "macros.h"
#include "misc.h"

// How long to keep the touchscreen on for config->touchDisableWhileInactive
#define TOUCH_ENABLED_DURATION_MS (3000)  // note this 3s value is mentioned in config.json

// The threshold between inner and outer ring
#define THRESHOLD_RADIUS ((PBL_DISPLAY_WIDTH * 8) / 35)
#define TOUCH_DIAL_CENTER_Y_OFFSET (21)


static Layer* s_layer = NULL;
static AppTimer* s_cancel_timer = NULL;

static TouchSelectionCallback s_callback = NULL;
static TouchServiceHandler s_parent_handler = NULL; // optional additional handler specified by this layer's parent

typedef enum SelectMode {
    SELECTMODE_NONE = 0,
    SELECTMODE_HOUR,  // The first touch of two
    SELECTMODE_MINUTE,  // The second touch of two
    SELECTMODE_WINDUP,  // The first touch of one
} SelectMode;
static SelectMode s_select_mode = SELECTMODE_NONE;

static bool s_touch_is_enabled = false;
static bool s_is_duration = false;
static int8_t s_selected_hours = -1;
static int8_t s_selected_minutes = -1;
static int8_t s_selected_seconds = -1;
static uint32_t s_selected_angle = 0;
// Like s_selected_angle, but always shifted by the same fixed half-minute-segment offset that
// selected_segment() uses to quantize minutes/seconds, regardless of the num_segments passed to
// selected_segment() for the current call. Used for windup crossing detection instead of the raw
// s_selected_angle, so hour/minute carries fire exactly when the displayed value flips, not before.
static uint32_t s_windup_angle = 0;
static bool s_windup_on_inner_touch = false;

typedef enum TouchArea {
    TOUCH_AREA_NONE = 0,
    TOUCH_AREA_INNER,
    TOUCH_AREA_OUTER
} TouchArea;
static TouchArea s_touch_area = TOUCH_AREA_NONE;


/******************************************************************************
 Generic funcs
******************************************************************************/

// Return the euclidian distance squared between point a and b
static inline uint16_t distance_squared(GPoint a, GPoint b) {
    return SQUARE(a.x - b.x) + SQUARE(a.y - b.y);
}

static uint32_t normalize_angle(int32_t angle) {
    uint32_t normalized_angle = ABS(angle) % TRIG_MAX_ANGLE;
    if (angle < 0) {
        normalized_angle = TRIG_MAX_ANGLE - normalized_angle;
    }
    return normalized_angle;
}

// Return the angle from a to b in TRIGANGLE units
static inline int32_t angle_between_points(GPoint a, GPoint b) {
    return atan2_lookup(b.x - a.x, b.y - a.y);  // TODO why are x and y reversed ???????
}

// Convert an angle calculated from GPoints into screenspace by reflecting on the y-axis
// i.e. so that 0 degrees is straight up on the screen, instead of down
static inline uint32_t angle_to_screenspace(int32_t angle) {
    return normalize_angle(DEG_TO_TRIGANGLE(180) - angle);
}

static GPoint layer_get_center(Layer* layer) {
    const GRect bounds = layer_get_bounds(layer);
    GPoint center = grect_center_point(&bounds);
    center.y += TOUCH_DIAL_CENTER_Y_OFFSET;
    return center;
}


/******************************************************************************
 Graphics
******************************************************************************/

#define MAX_TEXT_SIZE (50)
static char s_central_text[MAX_TEXT_SIZE] = "hello";

static AppTimer* s_animation_timer = NULL;

#define FRAMERATE (30)
#define MS_PER_FRAME (MS_PER_S / FRAMERATE)
#define APPEAR_DURATION_MS (100)
#define CIRCLE_PERCENT_GROWTH_RATE (100 / (APPEAR_DURATION_MS / MS_PER_FRAME))
static int32_t s_circle_percent = 0;

static bool s_is_windup_seconds = false;
static bool s_is_windup_seconds_enablable = false;
static bool is_mode_windup_seconds(void) {
    return (s_select_mode == SELECTMODE_WINDUP) && s_is_windup_seconds;
}

static inline GColor color_outer_bg(void) {
    return config_get()->bgColor;
}
static inline GColor color_outer_fg(void) {
    return config_get()->textColor;
}
static inline GColor color_inner_bg(void) {
    return config_get()->textColor;
}
static inline GColor color_inner_fg(void) {
    return config_get()->bgColor;
}

static inline void reset_circle_radius(void) {
    s_circle_percent = 0;
}

static inline bool is_circle_finished_growing(void) {
    return s_circle_percent == 100;
}

static void cancel_animation_timer(void) {
    if (s_animation_timer != NULL) {
        app_timer_cancel(s_animation_timer);
        s_animation_timer = NULL;
    }
}

static void circle_shrink(void* context) {
    s_animation_timer = NULL;
    s_circle_percent = MAX(0, s_circle_percent - CIRCLE_PERCENT_GROWTH_RATE);
    if (s_circle_percent > 0) {
        s_animation_timer = app_timer_register(MS_PER_FRAME, circle_shrink, NULL);
    } else {
        layer_set_hidden(s_layer, true);
    }
    layer_mark_dirty(s_layer);
}
static void circle_grow(void* context) {
    s_animation_timer = NULL;
    s_circle_percent = MIN(100, s_circle_percent + CIRCLE_PERCENT_GROWTH_RATE);
    if (s_circle_percent < 100) {
        s_animation_timer = app_timer_register(MS_PER_FRAME, circle_grow, NULL);
    }
    layer_mark_dirty(s_layer);
}

static void animate_circle(bool appear) {
    cancel_animation_timer();
    if (appear) {
        circle_grow(NULL);
    } else {
        circle_shrink(NULL);
    }
}

static void draw_arrow_head(GContext *ctx, GPoint tip, int32_t direction_angle, int16_t len) {
    const int32_t spread = DEG_TO_TRIGANGLE(30);
    graphics_draw_line(ctx, tip,
        point_from_angle(tip, direction_angle + DEG_TO_TRIGANGLE(180) - spread, len)
    );
    graphics_draw_line(ctx, tip,
        point_from_angle(tip, direction_angle + DEG_TO_TRIGANGLE(180) + spread, len)
    );
}

// Draw a static arc arrow outside the inner circle, with arrow heads at both ends.
static void draw_windup_hint(const GRect* bounds, const GPoint* centre, GContext *ctx) {
    const int16_t hint_radius = THRESHOLD_RADIUS + 10;
    const int16_t arc_radius = hint_radius + 1;
    const int32_t arc_start = DEG_TO_TRIGANGLE(310);
    const int32_t arc_end = DEG_TO_TRIGANGLE(50);

    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);
    const int32_t arc_step = DEG_TO_TRIGANGLE(8);
    int32_t angle = arc_start;
    GPoint prev = point_from_angle(*centre, angle, arc_radius);
    while (true) {
        uint32_t dist_to_end = (uint32_t)((arc_end - angle + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE);
        int32_t next = (dist_to_end <= (uint32_t)arc_step)
            ? arc_end
            : (angle + arc_step) % TRIG_MAX_ANGLE;
        GPoint cur = point_from_angle(*centre, next, arc_radius);
        graphics_draw_line(ctx, prev, cur);
        if (next == arc_end) { break; }
        angle = next;
        prev = cur;
    }

    const GPoint start_tip = point_from_angle(*centre, arc_start, hint_radius);
    const GPoint end_tip = point_from_angle(*centre, arc_end, hint_radius);
    graphics_context_set_stroke_width(ctx, 2);
    draw_arrow_head(ctx, start_tip, arc_start - DEG_TO_TRIGANGLE(90), 9);
    draw_arrow_head(ctx, end_tip, arc_end + DEG_TO_TRIGANGLE(90), 9);
}

static void draw_background(const GPoint* centre, GContext *ctx) {
    graphics_context_set_antialiased(ctx, false);
    graphics_context_set_fill_color(ctx, color_outer_bg());
    graphics_fill_rect(ctx, GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT), 0, GCornerNone);
    graphics_color_circle(ctx, *centre, MUL_FRACT(THRESHOLD_RADIUS, s_circle_percent, 100), color_inner_bg());
    graphics_context_set_antialiased(ctx, true);
}

static void draw_selection_angle(const GPoint* centre, GContext *ctx) {
    const GPoint line_inner = point_from_angle(*centre, s_selected_angle, THRESHOLD_RADIUS / 2);

    const int32_t outer_length = (s_touch_area == TOUCH_AREA_OUTER) ? (PBL_DISPLAY_WIDTH / 2) : THRESHOLD_RADIUS;
    const GPoint line_outer = point_from_angle(*centre, s_selected_angle, outer_length);

    const GColor color = (s_touch_area == TOUCH_AREA_OUTER) ? config_get()->ringColorRemaining
                                                            : config_get()->ringColorOvertime;

    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, line_inner, line_outer);
}

#define NUM_CLOCK_INDICES (12)
static void draw_clock_indices(const GRect* bounds, GContext *ctx) {
    static const char* hours_text[NUM_CLOCK_INDICES] =
        {"12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
    static const char* mins_text[NUM_CLOCK_INDICES] =
        {"0", "5", "10", "15", "20", "25", "30", "35", "40", "45", "50", "55"};
    const GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    graphics_context_set_text_color(ctx, color_outer_fg());

#if INDEX_LINES
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, color_outer_fg());
#endif // INDEX_LINES

    for (int32_t i = 0; i < NUM_CLOCK_INDICES; i++) {
        const int16_t angle = i * DEG_TO_TRIGANGLE(360 / NUM_CLOCK_INDICES);

#define INDEX_LINES 0
#if INDEX_LINES
        const GPoint circumference_point = gpoint_from_polar(*bounds, GOvalScaleModeFitCircle, angle);
        int32_t line_len = ((i % 3) == 0) ? 10 : 5;
        const GPoint inner_point = point_from_angle(circumference_point, angle, -line_len);
        graphics_draw_line(ctx, circumference_point, inner_point);
#endif // INDEX_LINES

        const GPoint number_point = gpoint_from_polar(grect_crop(*bounds, 27), GOvalScaleModeFitCircle, angle);
        const char* text = (
            (s_is_duration && (i == 0)) ? "0"
            : (s_select_mode == SELECTMODE_HOUR) ? hours_text[i]
            : mins_text[i]
        );
        GSize txt_sz = graphics_text_layout_get_content_size(
            text, font, GRect(0, 0, 40, 200), GTextOverflowModeWordWrap, GTextAlignmentCenter);
        // GOTHIC reserves headroom above the caps (see multitap_keyboard.c's font ladder), so a
        // measured content box still sits low when centered; lift it back to the optical middle.
        const int rise = 4;
        const GRect text_bounds = {
            .origin = {number_point.x - txt_sz.w / 2, number_point.y - txt_sz.h / 2 - rise},
            .size = txt_sz
        };
        graphics_draw_text(ctx, text, font, text_bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
}

static void update_selection_text(void) {
    if (s_selected_hours < 0) {
        snprintf(s_central_text, sizeof(s_central_text), s_is_duration ? "- - : - - : - -" : "--:--");
    } else if (s_selected_minutes < 0) {
        const char* fmt = (s_is_duration ? "- - : - - : - -" : "%d:--");
        snprintf(s_central_text, sizeof(s_central_text), fmt, s_selected_hours);
    } else if (!is_mode_windup_seconds()) {
        const char* fmt = (s_is_duration ? "%02d:%02d:00" : "%d:%02d");
        snprintf(s_central_text, sizeof(s_central_text), fmt, s_selected_hours, s_selected_minutes);
    } else {
        if (s_selected_seconds < 0) {
            snprintf(s_central_text, sizeof(s_central_text), "%02d:%02d:--", s_selected_hours, s_selected_minutes);
        } else {
            snprintf(s_central_text, sizeof(s_central_text), "%02d:%02d:%02d",
                     s_selected_hours, s_selected_minutes, s_selected_seconds);
        }
    }
}

// Same title-row layout as the box-style dial's head row (see main.c's dial_update_proc):
// "Duration" trailing-ellipsis on the left, the current value trailing-ellipsis on the right.
static void draw_title_row(const GRect* bounds, GContext* ctx) {
    const GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "Duration", font, GRect(4, 2, bounds->size.w - 90, 26),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, s_central_text, font, GRect(4, 2, bounds->size.w - 8, 26),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

static void draw_layer(Layer* layer, GContext* ctx) {
    const GRect bounds = layer_get_bounds(layer);
    const GRect shifted_bounds = GRect(bounds.origin.x, bounds.origin.y + TOUCH_DIAL_CENTER_Y_OFFSET,
                                       bounds.size.w, bounds.size.h);
    const GPoint centre = grect_center_point(&shifted_bounds);
    draw_background(&centre, ctx);
    draw_title_row(&bounds, ctx);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "Cancel", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(centre.x - THRESHOLD_RADIUS, centre.y - 16,
                             THRESHOLD_RADIUS * 2, 32),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    if (is_circle_finished_growing()) {
        if (s_touch_area != TOUCH_AREA_NONE) {
            draw_selection_angle(&centre, ctx);
        }
        if (s_select_mode == SELECTMODE_WINDUP) {
            draw_windup_hint(&shifted_bounds, &centre, ctx);
        }
        draw_clock_indices(&shifted_bounds, ctx);
    }
}


/******************************************************************************
 Logic
******************************************************************************/

// state tracking for debouncing
static time_t s_edge_time_s = 0;  // the time of the latest TouchEvent_Touchdown/Liftoff
static uint16_t s_edge_time_ms = 0;  // ms since s_edge_time_s of the latest TouchEvent_Touchdown/Liftoff

static inline void register_touch_edge(void) {
    (void)time_ms(&s_edge_time_s, &s_edge_time_ms);
}

static inline bool is_touch_state_long_enough(int32_t minDurationMs) {
    time_t now_s = 0;
    uint16_t now_ms = 0;
    (void)time_ms(&now_s, &now_ms);

    const int32_t duration_ms = (
        ((now_s - s_edge_time_s) * MS_PER_S)
        + ((int32_t)now_ms - (int32_t)s_edge_time_ms)
    );
    LOG("touch state duration: %d:%u - %d:%u = %d", s_edge_time_s, s_edge_time_ms, now_s, now_ms, duration_ms);
    return duration_ms >= minDurationMs;
}

static bool is_touch_in_outer_ring(GPoint touch) {
    return distance_squared(layer_get_center(s_layer), touch) > SQUARE(THRESHOLD_RADIUS);
}

// Return the index of the radial segment selected by `touch`.
// Also update s_selected_angle for drawing the selection line.
static uint8_t selected_segment(GPoint touch, uint8_t num_segments) {
    const GPoint centre = layer_get_center(s_layer);

    const int32_t selected_angle_gpointspace = angle_between_points(centre, touch);
    s_selected_angle = angle_to_screenspace(selected_angle_gpointspace);

    // Fixed offset matching the 60-segment (minute/second) quantization used in SELECTMODE_WINDUP,
    // regardless of num_segments for this particular call - keeps windup crossing detection in
    // sync with whichever minute/second value is actually displayed.
    const int32_t windup_offset = TRIG_MAX_ANGLE / (2 * 60);
    s_windup_angle = angle_to_screenspace(selected_angle_gpointspace - windup_offset);

    // LOG("(%d, %d) -> (%d, %d) = %d degrees", centre.x, centre.y, touch.x, touch.y, TRIGANGLE_TO_DEG(s_selected_angle));

    // The centre of the zeroth segment is at 0 screenspace degrees (i.e. straight up)
    // so subtract half a segment from the touched angle
    const int32_t offset = TRIG_MAX_ANGLE / (2 * num_segments);
    const uint32_t offset_angle_screenspace = angle_to_screenspace(selected_angle_gpointspace - offset);

    return MUL_FRACT(offset_angle_screenspace, num_segments, TRIG_MAX_ANGLE);
}

// Increment or decrement hours when 12o'clock is passed.
// To avoid accidental windup, only enable windup on the inner touch area after passing the 20-minute mark.
// Entering seconds windup requires passing the 20-minute mark and then 12o'clock, both anticlockwise
// and both while on the outer ring; touching the inner ring in between cancels the attempt.
// Once in seconds windup, switch back to mins/hours at 5 mins.
static void apply_windup(uint32_t prev_angle, uint32_t current_angle) {
    if (s_touch_area == TOUCH_AREA_INNER) {
        s_is_windup_seconds_enablable = false;
    }
    if (s_select_mode == SELECTMODE_WINDUP) {
        const bool crossed_12_clockwise = (
            prev_angle > DEG_TO_TRIGANGLE(315)) && (current_angle < DEG_TO_TRIGANGLE(45));
        const bool crossed_12_anticlockwise = (
            current_angle > DEG_TO_TRIGANGLE(315)) && (prev_angle < DEG_TO_TRIGANGLE(45));
        // 120 degrees == the 20-minute mark (20/60 of the dial).
        const bool crossed_20min_clockwise = (
            WITHIN_EXCL(prev_angle, DEG_TO_TRIGANGLE(75), DEG_TO_TRIGANGLE(120))
            && WITHIN(current_angle, DEG_TO_TRIGANGLE(120), DEG_TO_TRIGANGLE(165))
        );
        const bool crossed_20min_anticlockwise = (
            WITHIN_EXCL(current_angle, DEG_TO_TRIGANGLE(75), DEG_TO_TRIGANGLE(120))
            && WITHIN(prev_angle, DEG_TO_TRIGANGLE(120), DEG_TO_TRIGANGLE(165))
        );

        if (crossed_12_clockwise) {
            if ((s_touch_area == TOUCH_AREA_OUTER) || s_windup_on_inner_touch) {
                // increment
                if (s_is_windup_seconds) {
                    s_selected_minutes ++;
                    if (s_selected_minutes >= 5) {
                        // cancel seconds windup
                        s_is_windup_seconds = false;
                        s_is_windup_seconds_enablable = false;
                        s_selected_minutes = -1;
                        s_selected_seconds = -1;
                    }
                } else {
                    s_selected_hours ++;
                }
            }
        } else if (crossed_12_anticlockwise) {
            if (s_is_windup_seconds_enablable && !s_is_windup_seconds && (s_selected_hours == 0)
                && (s_touch_area == TOUCH_AREA_OUTER)) {
                // enable seconds windup
                s_is_windup_seconds = true;
                s_selected_hours = 0;
                s_selected_minutes = 0;
            } else {
                // decrement
                if (s_is_windup_seconds) {
                    s_selected_minutes = MAX(0, s_selected_minutes - 1);
                } else {
                    s_selected_hours = MAX(0, s_selected_hours - 1);
                }
            }
        } else if (crossed_20min_clockwise) {
            s_windup_on_inner_touch = true;
        } else if (crossed_20min_anticlockwise && (s_selected_hours == 0)
                   && (s_touch_area == TOUCH_AREA_OUTER)) {
            s_is_windup_seconds_enablable = true;
        }
    }
}

static void update_selection(GPoint touch) {
    const uint32_t prev_windup_angle = s_windup_angle;

    if (s_touch_area == TOUCH_AREA_OUTER) {
        if (s_select_mode == SELECTMODE_HOUR) {
            s_selected_hours = selected_segment(touch, 12);
            if (!s_is_duration && (s_selected_hours == 0)) {
                s_selected_hours = 12;
            }
        } else if (!is_mode_windup_seconds()){
            s_selected_minutes = selected_segment(touch, 60);
            s_selected_seconds = 0;
        } else {
            s_selected_seconds = selected_segment(touch, 60);
        }
    } else {  // TOUCH_AREA_INNER; no selection
        (void)selected_segment(touch, 1);  // update s_selected_angle
        if (s_select_mode == SELECTMODE_HOUR) {
            s_selected_hours = -1;
        } else if (!is_mode_windup_seconds()){
            s_selected_minutes = -1;
        } else {
            s_selected_seconds = -1;
        }
    }

    apply_windup(prev_windup_angle, s_windup_angle);

    update_selection_text();
}

static void cancel_timeout(void) {
    if (s_cancel_timer != NULL) {
        app_timer_cancel(s_cancel_timer);
        s_cancel_timer = NULL;
    }
}

static void handle_timeout(void* data);
static void start_timeout(void) {
    cancel_timeout();
    int32_t timeout_ms = config_get()->touchInputTimeoutDeciseconds * 100;
    if (timeout_ms > 0) {
        s_cancel_timer = app_timer_register(timeout_ms, handle_timeout, NULL);
    }
}

static void finish(void) {
    cancel_timeout();
    s_select_mode = SELECTMODE_NONE;
    s_selected_hours = -1;
    s_selected_minutes = -1;
    s_selected_seconds = -1;
    s_touch_area = TOUCH_AREA_NONE;
    s_windup_on_inner_touch = false;
    s_is_windup_seconds = false;
    s_is_windup_seconds_enablable = false;
    animate_circle(false);
}

static void handle_timeout(void* data) {
    UNUSED(data);
    LOG("Timedout touch selection");
    s_cancel_timer = NULL;
    finish();
}

static void handle_touch_event(const TouchEvent *event, void *context) {
    UNUSED(context);
    touch_enable(true);  // reset any disable timer
    const GPoint touch = (GPoint){event->x, event->y};
    s_touch_area = is_touch_in_outer_ring(touch) ? TOUCH_AREA_OUTER : TOUCH_AREA_INNER;

    switch (event->type) {
    case TouchEvent_Touchdown:
        s_selected_angle = 0;  // reset to avoid triggering windup
        s_windup_angle = 0;  // reset to avoid triggering windup
        cancel_timeout();
        if (s_select_mode == SELECTMODE_NONE) {
            // In this app's dial flow, touch input always edits timer duration.
            s_is_duration = true;
            if ((config_get()->touchTimerSetMethod == TouchTimerSetMethod_TwoTouch) || !s_is_duration) {
                s_select_mode = SELECTMODE_HOUR;
            } else {
                s_select_mode = SELECTMODE_WINDUP;
                s_selected_hours = 0;
            }
            animate_circle(true);
            layer_set_hidden(s_layer, false);
        } else {
            ASSERT(s_select_mode == SELECTMODE_MINUTE);
            if (!is_touch_state_long_enough(config_get()->touchLiftMinDurationMs)) {  // liftoff too short
                s_select_mode = SELECTMODE_HOUR;
            }
        }
        update_selection(touch);
        register_touch_edge();
        break;
    case TouchEvent_PositionUpdate:
        update_selection(touch);
        break;
    case TouchEvent_Liftoff:
        if (s_touch_area == TOUCH_AREA_OUTER) {
            if (is_touch_state_long_enough(config_get()->touchMinDurationMs)) {
                update_selection(touch);
                if (s_select_mode == SELECTMODE_HOUR) {
                    start_timeout();
                    s_select_mode = SELECTMODE_MINUTE;
                } else {  // SELECTMODE_MINUTE(WINDUP)
                    LOG("Complete touch selection");
                    ASSERT(s_selected_seconds != -1);
                    s_callback(s_is_duration, s_selected_hours, s_selected_minutes, s_selected_seconds);
                    finish();
                }
            } else {  // touch too short
                LOG("Short touch ignored");
                if (s_select_mode == SELECTMODE_MINUTE) {  // second touch
                    s_selected_minutes = -1;
                    update_selection_text();
                    start_timeout();
                } else {  // first touch
                    finish();
                }
            }
        } else {  // TOUCH_AREA_INNER
            LOG("Cancelled touch selection");
            finish();
        }
        s_touch_area = TOUCH_AREA_NONE;
        register_touch_edge();
        break;
    default:
        ASSERT(false);
        break;
    }

    layer_mark_dirty(s_layer);

    if (s_parent_handler != NULL) {
        s_parent_handler(event, context);
    }
}

// Timer for config_get()->touchDisableWhileInactive
static AppTimer* s_touch_disable_timer = NULL;
static void timeout_enable_callback(void* context) {
    UNUSED(context);
    s_touch_disable_timer = NULL;
    touch_enable(false);
}
static void schedule_touch_disable(void) {
    const uint32_t timeout_ms = TOUCH_ENABLED_DURATION_MS;
    if (s_touch_disable_timer == NULL) {
        s_touch_disable_timer = app_timer_register(timeout_ms, timeout_enable_callback, NULL);
        ASSERT(s_touch_disable_timer != NULL);
    } else {
        const bool success = app_timer_reschedule(s_touch_disable_timer, timeout_ms);
        ASSERT(success);
    }
}


/******************************************************************************
 Public funcs
******************************************************************************/

// Return true if the user is currently touching the screen
bool touch_in_progress(void) {
    return s_select_mode != SELECTMODE_NONE;
}

// Enable or disable this module's touchscreen handler
void touch_enable(bool enable) {
    ASSERT(s_layer != NULL);
    if (touch_service_is_enabled() && (s_layer != NULL)) {
        if (enable) {
            if (!s_touch_is_enabled) {
                touch_service_subscribe(handle_touch_event, NULL);
                s_touch_is_enabled = true;
                    LOG("touch ON");
            }
            if (config_get()->touchDisableWhileInactive) {
                schedule_touch_disable();
            }
        } else {
            finish();
            touch_service_unsubscribe();
            s_touch_is_enabled = false;
            LOG("touch OFF");
        }
    }
}

void touch_create(Layer* parent, TouchSelectionCallback callback, TouchServiceHandler handler) {
    ASSERT(s_layer == NULL);
    if (touch_service_is_enabled() && (s_layer == NULL)) {
        // primary layer
        s_layer = layer_create(layer_get_frame(parent));
        layer_set_update_proc(s_layer, draw_layer);
        layer_add_child(parent, s_layer);

        layer_set_hidden(s_layer, true);

        s_callback = callback;
        s_parent_handler = handler;
    }
}

void touch_destroy(void) {
    ASSERT(s_layer != NULL);  // can happen if !touch_service_is_enabled()
    if (s_layer != NULL) {
        touch_enable(false);
        layer_destroy(s_layer);
        s_layer = NULL;
        cancel_animation_timer();
        cancel_timeout();
    }
}


#endif // PBL_TOUCH
