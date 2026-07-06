// SPDX-License-Identifier: GPL-3.0-only

#include "config.h"

static const Config s_config = {
    .textColor = GColorBlack,
    .bgColor = GColorWhite,
    .ringColorRemaining = GColorBlack,
    .ringColorOvertime = GColorBlack,
    .touchInputTimeoutDeciseconds = 20,
    .touchMinDurationMs = 150,
    .touchZoneAssignment = TouchZoneAssignment_Invert,
    .touchTimerMode = TouchTimerEffect_Duration,
    .touchTimerSetMethod = TouchTimerSetMethod_MinuteWindup,
    .touchDisableWhileInactive = false,
    .touchLiftMinDurationMs = 100,
};

const Config* config_get(void) {
    return &s_config;
}
