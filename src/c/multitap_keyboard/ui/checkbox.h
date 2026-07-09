#pragma once
#include <pebble.h>

// Checkbox, with selected=true for highlighted rows.
void ui_draw_checkbox(GContext *ctx, GRect box, bool checked, bool selected);

// Bare checkmark tick, with selected=true for highlighted rows.
void ui_draw_check(GContext *ctx, GRect box, bool selected);
