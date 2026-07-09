#pragma once
#include <pebble.h>

// Shared section-header style for lists and static views.
// `gap_above` adds list-background spacing before the band.

int16_t ui_section_height(bool gap_above);
void    ui_section_draw(GContext *ctx, GRect bounds, const char *title, bool gap_above);
