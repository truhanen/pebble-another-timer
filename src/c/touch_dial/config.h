// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <pebble.h>

typedef enum TouchZoneAssignment {
    TouchZoneAssignment_Default = 0,  // inner=duration, outer=alarm
    TouchZoneAssignment_Invert = 1,   // inner=alarm, outer=duration
} TouchZoneAssignment;

typedef enum TouchTimerEffect {
    TouchTimerEffect_Clear = 0,
    TouchTimerEffect_Duration = 1,
    TouchTimerEffect_Remaining = 2,
} TouchTimerEffect;

typedef enum TouchTimerSetMethod {
    TouchTimerSetMethod_MinuteWindup = 0,
    TouchTimerSetMethod_TwoTouch = 1,
} TouchTimerSetMethod;

typedef struct Config {
    GColor textColor;
    GColor bgColor;
    GColor ringColorRemaining;
    GColor ringColorOvertime;
    int32_t touchInputTimeoutDeciseconds;
    int32_t touchMinDurationMs;
    TouchZoneAssignment touchZoneAssignment;
    TouchTimerEffect touchTimerMode;
    TouchTimerSetMethod touchTimerSetMethod;
    bool touchDisableWhileInactive;
    int32_t touchLiftMinDurationMs;
} Config;

const Config* config_get(void);
