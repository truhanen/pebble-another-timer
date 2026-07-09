#pragma once
#include <pebble.h>

// =============================================================================
// ui_theme — the shared color palette for the ui lib. Nine tokens cover every
// surface the lib (and the app) draws:
//
//   background     the window / menu fill
//   text           primary ink on the background
//   text_muted     secondary or disabled ink (a softer gray)
//   neutral        a low-contrast surface fill (zebra stripe, key cap, soft focus)
//   accent         selection highlight / checked tick fill
//   accent_text    ink drawn on top of the accent
//   danger         destructive / negative signal (a miss, a delete)
//   danger_light   a soft danger surface fill (danger ink reads on top of it)
//   danger_darker  the pressed / drop-shadow shade of a danger surface
//
// Keep each fg/bg pair legible against each other: text on background,
// accent_text on accent, and danger on danger_light (a dark fill with a bright
// danger ink, or a light fill with a strong one).
//
// The `danger*` tokens are the ONE sanctioned set of *semantic* theme tokens:
// they carry meaning (destructive action) yet are themed, so a re-theme tunes
// the red to fit each palette instead of leaving a clashing hardcoded GColorRed.
// Every OTHER meaning-bearing color (a yellow crown, a podium tint) still stays
// hardcoded at the call site so a re-theme can't strip its meaning — only danger
// earned tokens because it recurs across surfaces (miss, delete).
//
// The danger tokens are also OPTIONAL: leave any of them GColorClear (the zero
// value) and its getter falls back to a built-in red ramp (DarkCandyAppleRed /
// Melon / BulgarianRose). On Pebble's tiny palette red-is-red, so the common
// case configures only background/accent and writes nothing for danger; a
// palette that genuinely needs a different red can still override them.
//
// Set once at startup via ui_theme_set(); left unconfigured it is the stock
// MenuLayer look (black ink on white, black highlight) so the lib is usable
// without theming.
// =============================================================================

typedef struct {
  GColor background;     // window / menu fill
  GColor text;           // primary ink
  GColor text_muted;     // secondary / disabled ink
  GColor neutral;        // soft low-contrast surface fill
  GColor accent;         // selection highlight / checked tick fill
  GColor accent_text;    // ink drawn on the accent
  GColor danger;         // destructive / negative signal (miss, delete) — optional
  GColor danger_light;   // soft danger surface fill (danger ink reads on it) — optional
  GColor danger_darker;  // pressed / drop-shadow shade of a danger surface — optional
} UiTheme;

void    ui_theme_set(UiTheme theme);
UiTheme ui_theme_get(void);

// Shorthands for each color.
GColor  ui_background(void);
GColor  ui_text(void);
GColor  ui_text_muted(void);
GColor  ui_neutral(void);
GColor  ui_accent(void);
GColor  ui_accent_text(void);
GColor  ui_danger(void);
GColor  ui_danger_light(void);
GColor  ui_danger_darker(void);
