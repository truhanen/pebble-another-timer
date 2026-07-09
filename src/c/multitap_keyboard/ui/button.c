#include "button.h"
#include "ui_align.h"
#include "ui_theme.h"

#define ICON_GUT 6   // gap from the box edge to a left-tucked icon

// A non-clear override color (GColorClear is the zero/"unset" value; every real
// opaque color has its alpha bits set, so a non-zero argb means "overridden").
static bool has_override(GColor c) { return c.argb != 0; }

// Recolor a palettized icon so its opaque pixels become `ink` (mirrors the
// helpers in list_item.c / footer.c). No-op on a non-palettized asset.
static void tint_icon(GBitmap *bmp, GColor ink) {
  if (!bmp) return;
  GColor *pal = gbitmap_get_palette(bmp);
  if (!pal) return;
  int n;
  switch (gbitmap_get_format(bmp)) {
    case GBitmapFormat1BitPalette: n = 2;  break;
    case GBitmapFormat2BitPalette: n = 4;  break;
    case GBitmapFormat4BitPalette: n = 16; break;
    default: return;
  }
  for (int i = 0; i < n; i++) if (pal[i].a != 0) pal[i] = ink;
}

// The scheme's three roles: the SOLID fill, the line (OUTLINE border / the ink
// for OUTLINE+GHOST), and the ink that reads on a SOLID fill.
static void scheme_colors(UiButtonScheme s, GColor *fill, GColor *line, GColor *on_fill) {
  switch (s) {
    case UI_BTN_PRIMARY:
      *fill = ui_accent(); *line = ui_accent(); *on_fill = ui_accent_text();
      break;
    case UI_BTN_DANGER:
      // danger carries no themed on-color; white reads on every sane danger fill
      // (this is the hand-tuned MISS-bar choice, now centralized).
      *fill = ui_danger(); *line = ui_danger(); *on_fill = GColorWhite;
      break;
    case UI_BTN_NEUTRAL:
    default:
      *fill = ui_neutral(); *line = ui_text(); *on_fill = ui_text();
      break;
  }
}

GColor ui_button_ink(const UiButtonSpec *spec) {
  if (has_override(spec->ink_override)) return spec->ink_override;
  if (spec->disabled)                   return ui_text_muted();
  GColor fill, line, on_fill;
  scheme_colors(spec->scheme, &fill, &line, &on_fill);
  // SOLID ink reads on the fill; OUTLINE/GHOST ink is the scheme's line color
  // (which for NEUTRAL is the primary text ink).
  return spec->style == UI_BTN_SOLID ? on_fill : line;
}

// The SOLID fill `spec` resolves to (override > disabled muting > scheme color).
static GColor solid_fill(const UiButtonSpec *spec) {
  if (has_override(spec->fill_override)) return spec->fill_override;
  if (spec->disabled)                   return ui_neutral();   // muted surface
  GColor fill, line, on_fill;
  scheme_colors(spec->scheme, &fill, &line, &on_fill);
  return fill;
}

// A drop-shadow shade: the fill stepped one level toward black. A shadow only
// has to be darker than the surface under it, so (unlike a same-hue tint) it may
// collapse all the way to black — on a dark theme it just melts into the
// background and the sink-on-press alone carries the feedback.
static GColor shadow_of(GColor fill) {
  fill.r = fill.r ? fill.r - 1 : 0;
  fill.g = fill.g ? fill.g - 1 : 0;
  fill.b = fill.b ? fill.b - 1 : 0;
  return fill;
}

// The shade a raised button sinks/casts into when no shadow_override is given. A
// themed DANGER fill has a hand-picked darker red (ui_danger_darker) that reads
// better than an algorithmic step; every other fill — including a custom
// fill_override, which has no themed darker variant — falls back to shadow_of().
static GColor default_shadow(const UiButtonSpec *spec, GColor fill) {
  if (spec->scheme == UI_BTN_DANGER && !has_override(spec->fill_override))
    return ui_danger_darker();
  return shadow_of(fill);
}

GRect ui_button_content_box(GRect box, const UiButtonSpec *spec) {
  bool raised = spec->elevation > 0 && spec->style != UI_BTN_GHOST;
  if (!raised) return box;
  // The face is `box` minus the elevation lip: it occupies the top-left at rest
  // and slides into the bottom-right (where the shadow sat) when pressed. Either
  // way the face AND its drop shadow stay inside `box` — the lip never spills
  // past the rect the caller reserved, so adjacent buttons can't be overdrawn.
  box.size.w -= spec->elevation;
  box.size.h -= spec->elevation;
  if (spec->pressed) {
    box.origin.x += spec->elevation;
    box.origin.y += spec->elevation;
  }
  return box;
}

void ui_button_draw(GContext *ctx, GRect box, const UiButtonSpec *spec) {
  GColor ink = ui_button_ink(spec);
  bool solid = spec->style == UI_BTN_SOLID;
  GColor fill = solid ? solid_fill(spec) : GColorClear;
  GColor shadow = has_override(spec->shadow_override) ? spec->shadow_override
                                                      : default_shadow(spec, fill);

  // 3D: a raised SOLID button casts a bottom-right drop shadow at rest. Pressing
  // it sinks the face into that shadow (see ui_button_content_box) AND fills it
  // with the shadow shade, so the button reads as pushed down into its own dark
  // recess — each button darkening to its own darker variant. The face is inset
  // by `elevation`, so the face + its shadow lip together fill `box` exactly and
  // never overflow it.
  bool raised = spec->elevation > 0 && spec->style != UI_BTN_GHOST;
  GRect face = ui_button_content_box(box, spec);   // inset; sunk on press

  if (raised && solid && !spec->pressed) {
    GRect shbox = face;
    shbox.origin.x += spec->elevation;
    shbox.origin.y += spec->elevation;
    graphics_context_set_fill_color(ctx, shadow);
    graphics_fill_rect(ctx, shbox, spec->radius, GCornersAll);
  }

  // 1) The shape.
  if (solid) {
    graphics_context_set_fill_color(ctx, (raised && spec->pressed) ? shadow : fill);
    graphics_fill_rect(ctx, face, spec->radius, GCornersAll);
    if (has_override(spec->border)) {      // optional hairline around the face
      graphics_context_set_stroke_color(ctx, spec->border);
      graphics_draw_round_rect(ctx, face, spec->radius);
    }
  } else if (spec->style == UI_BTN_OUTLINE) {
    graphics_context_set_stroke_color(ctx, ink);   // border matches OUTLINE ink
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, face, spec->radius);
    graphics_context_set_stroke_width(ctx, 1);
  } // GHOST draws no shape.

  bool has_label = spec->label && spec->label[0];

  // 2) The icon — centered alone, tucked to the left when a label follows.
  if (spec->icon) {
    tint_icon(spec->icon, ink);
    GSize is = gbitmap_get_bounds(spec->icon).size;
    // Vertically centered; horizontally centered alone, left-tucked with a label.
    GRect ir = ui_rect_align(face, is,
                             has_label ? UI_ALIGN_START : UI_ALIGN_CENTER,
                             UI_ALIGN_CENTER);
    if (has_label) ir.origin.x = face.origin.x + ICON_GUT;
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, spec->icon, ir);
  }

  // 3) The label — centered in the face, or in the face past the icon gutter.
  if (has_label) {
    GRect tb = face;
    if (spec->icon) {
      int cut = ICON_GUT + gbitmap_get_bounds(spec->icon).size.w;
      tb.origin.x += cut;
      tb.size.w   -= cut;
    }
    graphics_context_set_text_color(ctx, ink);
    ui_text_draw(ctx, spec->label, spec->font, tb,
                 GTextAlignmentCenter, true, GTextOverflowModeFill);
  }
}
