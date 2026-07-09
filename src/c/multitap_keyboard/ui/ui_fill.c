#include "ui_fill.h"

void ui_fill_dither(GContext *ctx, GRect rect, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  int x0 = rect.origin.x, y0 = rect.origin.y;
  int x1 = x0 + rect.size.w, y1 = y0 + rect.size.h;
  for (int y = y0; y < y1; y++)
    for (int x = x0 + (y & 1); x < x1; x += 2)   // checkerboard: offset every other row
      graphics_draw_pixel(ctx, GPoint(x, y));
}
