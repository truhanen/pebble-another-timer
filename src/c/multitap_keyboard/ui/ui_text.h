#pragma once
#include <pebble.h>

// =============================================================================
// ui_text — a font bundled with the metrics needed to place it. graphics_draw_text
// is top-aligned and the GOTHIC fonts pad above the caps, so vertically centering
// text needs two per-font numbers: the cap-box height to center on and a small
// optical nudge upward. Carrying those *with* the font means any component handed
// a UiFont centers correctly with zero magic numbers at the call site — which is
// also why a "font size" is just a UiFont (see the size presets below).
// =============================================================================

typedef struct {
  const char *key;    // a FONT_KEY_* system font
  int16_t     cap;    // cap-box height to vertically center on
  int16_t     nudge;  // optical upward correction (GOTHIC pads above the caps)
} UiFont;

// Presets so callers never hand-roll cap/nudge — one coherent Gothic ladder from
// micro to header. Number/display families (LECO, Bitham, Droid Serif) are left
// out on purpose: they're single-purpose and their metrics aren't tuned here;
// add a preset when one earns its keep.
//
// cap/nudge for the original five match what draw_centered / list_item / the multitap
// keyboard arrived at; the rest follow the same pattern (cap ≈ size + 4) and are
// only consulted by ui_text_draw(vcenter=true) — verify centering on first such
// use of a newly added font.
extern const UiFont UI_FONT_HEADER;       // GOTHIC_28_BOLD
extern const UiFont UI_FONT_TITLE;        // GOTHIC_24_BOLD
extern const UiFont UI_FONT_BODY_LARGE;   // GOTHIC_24 (roomy body / light title)
extern const UiFont UI_FONT_BODY;         // GOTHIC_18
extern const UiFont UI_FONT_BODY_BOLD;    // GOTHIC_18_BOLD
extern const UiFont UI_FONT_CAPTION;      // GOTHIC_14
extern const UiFont UI_FONT_CAPTION_BOLD; // GOTHIC_14_BOLD (section headers)
extern const UiFont UI_FONT_MICRO;        // GOTHIC_09

GFont   ui_font(UiFont f);
int16_t ui_font_cap(UiFont f);          // cap-box height (a row's natural text height)

// Draw `text` in `box`. With `vcenter`, the text is vertically centered in box
// via the font's metrics; otherwise it is top-aligned at box.origin.y.
void ui_text_draw(GContext *ctx, const char *text, UiFont f, GRect box,
                  GTextAlignment halign, bool vcenter, GTextOverflowMode overflow);
