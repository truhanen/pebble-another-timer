#include "ui_theme.h"

// Default = the stock MenuLayer look (black ink on white, black highlight), so
// an app that never calls ui_theme_set() looks exactly as before. Each GColor is
// built from its ARGB8 so this is a constant initializer (the GColor* macros are
// compound literals and can't sit in a static initializer).
// The danger tokens default to clear so that a partial ui_theme_set() (one that
// configures only background/accent) leaves them unset rather than wiped to a
// bad value — their getters then fall back to the built-in red ramp below.
static UiTheme s_theme = {
  .background    = { .argb = GColorWhiteARGB8 },
  .text          = { .argb = GColorBlackARGB8 },
  .text_muted    = { .argb = GColorDarkGrayARGB8 },
  .neutral       = { .argb = GColorLightGrayARGB8 },
  .accent        = { .argb = GColorBlackARGB8 },
  .accent_text   = { .argb = GColorWhiteARGB8 },
  .danger        = { .argb = GColorClearARGB8 },
  .danger_light  = { .argb = GColorClearARGB8 },
  .danger_darker = { .argb = GColorClearARGB8 },
};

// The built-in danger ramp, used when a token is left GColorClear (argb 0 = the
// zero/"unset" value; every opaque color has its alpha bits set). Red-is-red on
// Pebble, so most apps never override these.
#define DANGER_DEFAULT        ((GColor){ .argb = GColorDarkCandyAppleRedARGB8 })
#define DANGER_LIGHT_DEFAULT  ((GColor){ .argb = GColorMelonARGB8 })
#define DANGER_DARKER_DEFAULT ((GColor){ .argb = GColorBulgarianRoseARGB8 })

static GColor or_default(GColor c, GColor fallback) {
  return c.argb ? c : fallback;
}

void    ui_theme_set(UiTheme theme) { s_theme = theme; }
UiTheme ui_theme_get(void)          { return s_theme; }
GColor  ui_background(void)         { return s_theme.background; }
GColor  ui_text(void)              { return s_theme.text; }
GColor  ui_text_muted(void)        { return s_theme.text_muted; }
GColor  ui_neutral(void)           { return s_theme.neutral; }
GColor  ui_accent(void)             { return s_theme.accent; }
GColor  ui_accent_text(void)        { return s_theme.accent_text; }
GColor  ui_danger(void)            { return or_default(s_theme.danger,        DANGER_DEFAULT); }
GColor  ui_danger_light(void)      { return or_default(s_theme.danger_light,  DANGER_LIGHT_DEFAULT); }
GColor  ui_danger_darker(void)     { return or_default(s_theme.danger_darker, DANGER_DARKER_DEFAULT); }
