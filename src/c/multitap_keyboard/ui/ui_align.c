#include "ui_align.h"

static int16_t prv_offset(int16_t origin, int16_t box, int16_t content, UiAlign a) {
  switch (a) {
    case UI_ALIGN_END:    return origin + (box - content);
    case UI_ALIGN_CENTER: return origin + (box - content) / 2;
    case UI_ALIGN_START:
    default:              return origin;
  }
}

GRect ui_rect_align(GRect box, GSize content, UiAlign h, UiAlign v) {
  return GRect(prv_offset(box.origin.x, box.size.w, content.w, h),
               prv_offset(box.origin.y, box.size.h, content.h, v),
               content.w, content.h);
}
