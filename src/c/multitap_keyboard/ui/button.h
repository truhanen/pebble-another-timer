#pragma once
#include <pebble.h>
#include "ui_text.h"

// =============================================================================
// button — a themed, stateless button renderer for the ui lib.
//
// Two orthogonal axes describe every button:
//
//   style  — how the shape is treated:
//     UI_BTN_SOLID    a filled box; the ink reads on the fill
//     UI_BTN_OUTLINE  a 2px border in the scheme color, no fill
//     UI_BTN_GHOST    no box at all, just the label/icon ink
//
//   scheme — which themed palette the style is drawn in:
//     UI_BTN_PRIMARY  accent       (the app's selection / affirmative color)
//     UI_BTN_DANGER   danger       (destructive / negative — a miss, a delete)
//     UI_BTN_NEUTRAL  neutral/text (a quiet, low-emphasis key)
//
// Emphasis (focus / arm / press) is NOT a separate state flag — express it by
// choosing a louder style+scheme for that frame: a focused key is SOLID+PRIMARY,
// the same key at rest is GHOST+NEUTRAL. `disabled` is the one baked state; it
// mutes the ink (and softens a SOLID fill) regardless of style or scheme.
//
// The renderer is stateless: it owns no layer and loads nothing. Hand it a box
// and a spec and it draws. Icons are passed already-loaded — the caller owns the
// GBitmap and its lifecycle; the renderer tints the palette to the resolved ink,
// so a palettized asset recolors to match its scheme for free.
// =============================================================================

typedef enum { UI_BTN_SOLID, UI_BTN_OUTLINE, UI_BTN_GHOST } UiButtonStyle;
typedef enum { UI_BTN_PRIMARY, UI_BTN_DANGER, UI_BTN_NEUTRAL } UiButtonScheme;

typedef struct {
  UiButtonStyle  style;
  UiButtonScheme scheme;
  bool           disabled;

  const char    *label;    // NULL/"" for an icon-only or box-only button
  UiFont         font;     // font for `label` (unused without a label)
  GBitmap       *icon;     // NULL for none; caller-owned, tinted to the ink
  int16_t        radius;   // corner radius for SOLID/OUTLINE (0 = square)
  GColor         border;   // SOLID only: non-clear → hairline-stroke the face in
                           // this color (clear = no border, the default)

  // "3D" raised look. With elevation > 0 the button casts a bottom-right drop
  // shadow at rest and, when `pressed`, sinks by `elevation` px into where the
  // shadow sat AND fills with the shadow shade — reading as "pushed down into
  // its own dark recess." The shadow is SOLID-only (a hollow border casts none).
  // Both default to 0/false, so a button that ignores them stays flat as before.
  int16_t        elevation;  // shadow depth / press travel in px (0 = flat)
  bool           pressed;    // true → sunk into the shadow, filled with it

  // Escape hatch: a non-clear color here overrides the themed fill / ink for
  // this one draw. Left GColorClear (the zero value), the scheme's color wins.
  GColor         fill_override;
  GColor         ink_override;
  GColor         shadow_override;  // 3D shadow tint; clear → derived from the fill
} UiButtonSpec;

// Draw the button: the shape (per style/scheme/state), then the icon and/or
// label, in the resolved ink. An icon alone is centered; a label alone is
// centered; with both, the icon tucks to the left of the centered label. A
// GHOST button with no content draws nothing; a SOLID/OUTLINE one with no
// content draws an empty box to render custom content into (see ui_button_ink).
void ui_button_draw(GContext *ctx, GRect box, const UiButtonSpec *spec);

// The ink `spec` resolves to. Use it to draw custom content (a vector glyph, a
// multi-line value) over a box drawn by ui_button_draw, keeping that content
// on-theme without re-deriving the color rules.
GColor ui_button_ink(const UiButtonSpec *spec);

// The box the button's content actually occupies: `box` shifted down-right by
// `elevation` when the button is a pressed 3D button, else `box` unchanged. A
// caller that draws its own content over a box-only ui_button_draw must place it
// in this rect so the content sinks together with the box on press.
GRect ui_button_content_box(GRect box, const UiButtonSpec *spec);
