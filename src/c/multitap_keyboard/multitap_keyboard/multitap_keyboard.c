#include "multitap_keyboard.h"

// ---- Constants --------------------------------------------------------------
#define TEXT_AREA_H  44
#define CARET_BLINK_MS 500
#define CARET_IDLE_MS  4000   // rest the caret solid-on after this long without input
#define FLASH_MS       90
#define GRID_COLS    3
#define GRID_ROWS    4
#define KEY_COUNT    (GRID_COLS * GRID_ROWS)
#define BUFFER_CAP   96
#define LABEL_CAP    24
#define KEY_ELEV     2     // 3D key depth: raised by this many px, sunk on press
#define GRID_VPAD    2     // white breathing room above and below the key grid

#define KEY_SHIFT 9
#define KEY_SPACE 10
#define KEY_DEL   11

typedef enum { PAGE_ALPHA = 0, PAGE_NUMBERS, PAGE_SYMBOLS, PAGE_COUNT } Page;
typedef enum { SHIFT_OFF = 0, SHIFT_ONCE, SHIFT_LOCK } ShiftState;

// Each character key (0..8) cycles through a NULL-terminated glyph list (UTF-8).
static const char *const A0[] = { ".", ",", "?", "!", NULL };
static const char *const A1[] = { "a", "b", "c", NULL };
static const char *const A2[] = { "d", "e", "f", NULL };
static const char *const A3[] = { "g", "h", "i", NULL };
static const char *const A4[] = { "j", "k", "l", NULL };
static const char *const A5[] = { "m", "n", "o", NULL };
static const char *const A6[] = { "p", "q", "r", "s", NULL };
static const char *const A7[] = { "t", "u", "v", NULL };
static const char *const A8[] = { "w", "x", "y", "z", NULL };

static const char *const N0[] = { "1", NULL };  static const char *const N1[] = { "2", NULL };
static const char *const N2[] = { "3", NULL };  static const char *const N3[] = { "4", NULL };
static const char *const N4[] = { "5", NULL };  static const char *const N5[] = { "6", NULL };
static const char *const N6[] = { "7", NULL };  static const char *const N7[] = { "8", NULL };
static const char *const N8[] = { "9", NULL };

static const char *const S0[] = { ".", ",", "?", "!", NULL };
static const char *const S1[] = { ":", ";", "'", "\"", NULL };
static const char *const S2[] = { "@", "#", "$", NULL };
static const char *const S3[] = { "%", "&", "*", NULL };
static const char *const S4[] = { "+", "=", "-", NULL };
static const char *const S5[] = { "_", "/", "\\", NULL };
static const char *const S6[] = { "(", ")", "[", "]", NULL };
static const char *const S7[] = { "{", "}", "<", ">", NULL };
static const char *const S8[] = { "|", "^", "~", "`", NULL };

static const char *const *const PAGE_KEY[PAGE_COUNT][9] = {
  { A0, A1, A2, A3, A4, A5, A6, A7, A8 },
  { N0, N1, N2, N3, N4, N5, N6, N7, N8 },
  { S0, S1, S2, S3, S4, S5, S6, S7, S8 },
};
static const char *const PAGE_IND[PAGE_COUNT] = { "ABC", "123", "#+=" };

// Toggleable extended (non-ASCII) characters. Each is appended to a base key's
// cycle when enabled, and hidden from the key labels. All enabled by default.
// To extend the set, add entries here (glyph + which alpha key 0..8 it joins)
// and the case mapping in prv_shifted(), then bump EXT_COUNT.
typedef struct { const char *glyph; int key; } ExtCharDef;
#define EXT_COUNT 5
#define MAX_EFF   16
static const ExtCharDef EXT_DEFS[EXT_COUNT] = {
  { "å", 1 }, { "ä", 1 }, { "æ", 1 }, { "ö", 5 }, { "ø", 5 },
};

// Keyboard color themes. Each holds ARGB8 values (built into GColor at draw
// time): screen background, foreground (text, divider, labels), the soft resting
// fill of a letter key, the accent fill for active/pending keys and the function
// row, the text color drawn on that accent, and a danger TRIPLE for the DEL key —
// `danger` fills the key, `danger_light` inks its delete glyph (a light tint that
// reads on the fill), and `danger_darker` is the shadow it casts / the shade it
// sinks to on press. Modeled on the throw grid (the turn-result screen): a field
// of soft filled keys plus one bold accent strip, the destructive DEL key set
// apart as a solid danger button. Keep the triple a light->dark ramp.
typedef struct {
  const char *name;
  uint8_t bg, fg, key, accent, accent_text, danger, danger_light, danger_darker;
} Theme;

// The DEL key uses Mölkky's backspace ramp (DarkCandyAppleRed fill / Melon glyph
// / BulgarianRose shadow) across every theme — the same triple the "Mölkky" app
// skin gets from the ui lib's danger default — so the destructive key reads the
// same no matter which theme is picked. Light and Dark take Pebble's default
// accent (CobaltBlue); accent_text stays white so it reads on that blue.
static const Theme THEMES[] = {
  { "Light", GColorWhiteARGB8,      GColorBlackARGB8, GColorLightGrayARGB8, GColorCobaltBlueARGB8,    GColorWhiteARGB8, GColorDarkCandyAppleRedARGB8, GColorMelonARGB8, GColorBulgarianRoseARGB8 },
  { "Dark",  GColorBlackARGB8,      GColorWhiteARGB8, GColorDarkGrayARGB8,  GColorCobaltBlueARGB8,    GColorWhiteARGB8, GColorDarkCandyAppleRedARGB8, GColorMelonARGB8, GColorBulgarianRoseARGB8 },
  { "Ocean", GColorOxfordBlueARGB8, GColorWhiteARGB8, GColorDukeBlueARGB8,  GColorVividCeruleanARGB8, GColorBlackARGB8, GColorDarkCandyAppleRedARGB8, GColorMelonARGB8, GColorBulgarianRoseARGB8 },
  { "Mint",  GColorWhiteARGB8,      GColorBlackARGB8, GColorLightGrayARGB8, GColorIslamicGreenARGB8,  GColorWhiteARGB8, GColorDarkCandyAppleRedARGB8, GColorMelonARGB8, GColorBulgarianRoseARGB8 },
  { "Amber", GColorBlackARGB8,      GColorWhiteARGB8, GColorDarkGrayARGB8,  GColorChromeYellowARGB8,  GColorBlackARGB8, GColorDarkCandyAppleRedARGB8, GColorMelonARGB8, GColorBulgarianRoseARGB8 },
};
#define THEME_COUNT (int)(sizeof(THEMES) / sizeof(THEMES[0]))

// Optional app-supplied theme: a single shared slot, registered at runtime via
// multitap_keyboard_set_app_theme() and addressed as theme index THEME_COUNT (one
// past the built-ins). Lets a host app brand the keyboard from its shared
// UiTheme without editing this lib; set_app_theme folds that palette into this
// ARGB8 Theme so the draw path treats it exactly like a built-in.
static Theme s_app_theme;
static bool  s_has_app_theme;

struct MultitapKeyboard {
  Layer *layer;
  MultitapKeyboardDoneHandler done_handler;
  void *context;

  char buffer[BUFFER_CAP];
  int  max_len;             // byte cap on buffer (0 = only BUFFER_CAP limits)
  int  page;
  int  shift;               // ShiftState (explicit, user-driven)

  bool pending_active;
  int  pending_key;
  int  cycle_index;
  AppTimer *commit_timer;

  MultitapSettings settings;
  bool autocap_armed;       // derived: are we at a sentence start?
  bool autocap_suppressed;  // user pressed Shift to cancel the auto-capital
  bool ext_enabled[EXT_COUNT];
  int  theme;

  // Sticky text-field layout. prv_draw_text_fitted picks the loosest font/line
  // count that fits, but a wide pending glyph can force a wrap that a later,
  // narrower cycle glyph would undo — making the field flicker between one and
  // two lines mid-multitap. wrap_floor remembers the tightest level we've shown
  // so the layout never relaxes while the text only grows; it's reset when the
  // text shrinks (a delete), tracked via last_text_len.
  int  wrap_floor;
  int  last_text_len;

  bool caret_on;            // blinking caret visibility
  int  caret_idle;          // ms since last edit; blink rests on past CARET_IDLE_MS
  AppTimer *caret_timer;
  int  flash_key;           // cell to briefly highlight on tap (-1 = none)
  AppTimer *flash_timer;
  int  touch_key;           // cell currently held by the finger (-1 = none)

  GBitmap *icon_shift[3];   // indexed by ShiftState: off / once / lock
};

// ---- Case helpers -----------------------------------------------------------

static const char *prv_shifted(const char *g) {
  if (g[0] >= 'a' && g[0] <= 'z' && g[1] == '\0') {
    static char up[2]; up[0] = (char)(g[0] - 32); up[1] = '\0'; return up;
  }
  if (strcmp(g, "å") == 0) return "Å";
  if (strcmp(g, "ä") == 0) return "Ä";
  if (strcmp(g, "ö") == 0) return "Ö";
  if (strcmp(g, "æ") == 0) return "Æ";
  if (strcmp(g, "ø") == 0) return "Ø";
  return g;
}
static bool prv_has_case(const char *g) { return strcmp(prv_shifted(g), g) != 0; }

// Shift state actually in effect = explicit shift, or auto-cap when armed.
static int prv_effective_shift(MultitapKeyboard *kb) {
  if (kb->shift == SHIFT_LOCK) return SHIFT_LOCK;
  if (kb->shift == SHIFT_ONCE) return SHIFT_ONCE;
  if (kb->settings.auto_caps && kb->autocap_armed && !kb->autocap_suppressed)
    return SHIFT_ONCE;
  return SHIFT_OFF;
}

// Render glyph `g` in the case for an explicit shift state (for drawing key
// labels in a case that may differ from the in-effect shift — see prv_next_shift).
static void prv_render_glyph_cased(const char *g, int shift, char *out, size_t cap) {
  const char *src = (shift != SHIFT_OFF) ? prv_shifted(g) : g;
  strncpy(out, src, cap - 1); out[cap - 1] = '\0';
}

static void prv_render_glyph(MultitapKeyboard *kb, const char *g, char *out, size_t cap) {
  prv_render_glyph_cased(g, prv_effective_shift(kb), out, cap);
}

// The shift that applies to the NEXT new letter, for drawing keys OTHER than the
// active one. A pending multitap glyph has already claimed the in-effect shift —
// its case is locked while it keeps cycling — so a ONCE (explicit or auto-cap) is
// spent on it and only LOCK persists. With nothing pending this is just the
// in-effect shift, so resting keys are unchanged. This lets the pressed key stay
// capital ("ABC") while the rest drop to lowercase ("def") and Shift reads off.
static int prv_next_shift(MultitapKeyboard *kb) {
  if (!kb->pending_active) return prv_effective_shift(kb);
  return (kb->shift == SHIFT_LOCK) ? SHIFT_LOCK : SHIFT_OFF;
}

// Effective glyph list for (page,key): base glyphs plus any enabled extended
// characters assigned to this key (ALPHA only). Writes pointers into `out`.
static int prv_eff_glyphs(MultitapKeyboard *kb, int page, int key,
                          const char **out, int cap) {
  int n = 0;
  const char *const *base = PAGE_KEY[page][key];
  for (int j = 0; base[j] && n < cap; j++) out[n++] = base[j];
  if (page == PAGE_ALPHA) {
    for (int e = 0; e < EXT_COUNT && n < cap; e++) {
      if (kb->ext_enabled[e] && EXT_DEFS[e].key == key) out[n++] = EXT_DEFS[e].glyph;
    }
  }
  return n;
}

// ---- Buffer + sentence helpers ----------------------------------------------

static void prv_recompute_autocap(MultitapKeyboard *kb);   // defined below

static void prv_append(MultitapKeyboard *kb, const char *s) {
  size_t l = strlen(kb->buffer), sl = strlen(s);
  if (l + sl >= BUFFER_CAP) return;
  if (kb->max_len > 0 && l + sl > (size_t)kb->max_len) return;  // would overflow cap
  memcpy(kb->buffer + l, s, sl + 1);
}

static void prv_utf8_backspace(MultitapKeyboard *kb) {
  size_t len = strlen(kb->buffer);
  if (len == 0) return;
  size_t i = len - 1;
  while (i > 0 && ((unsigned char)kb->buffer[i] & 0xC0) == 0x80) i--;
  kb->buffer[i] = '\0';
}

static void prv_do_delete(MultitapKeyboard *kb) {
  if (kb->pending_active) { kb->pending_active = false; return; }
  prv_utf8_backspace(kb);
  prv_recompute_autocap(kb);
}

// True when the cursor sits at the start of a new sentence.
static bool prv_at_sentence_start(const char *buf) {
  int len = (int)strlen(buf);
  if (len == 0) return true;
  if (buf[len - 1] == '\n') return true;        // start of a fresh line
  if (buf[len - 1] != ' ') return false;        // mid-word, not a fresh start
  int i = len - 1;
  while (i >= 0 && buf[i] == ' ') i--;
  if (i < 0) return true;                        // only spaces so far
  char c = buf[i];
  return (c == '.' || c == '!' || c == '?' || c == '\n');
}

// Recompute auto-cap arming from the buffer; clear any one-letter suppression.
static void prv_recompute_autocap(MultitapKeyboard *kb) {
  kb->autocap_armed = prv_at_sentence_start(kb->buffer);
  kb->autocap_suppressed = false;
}

// ---- Commit / multi-tap -----------------------------------------------------

static void prv_emit_glyph(MultitapKeyboard *kb, const char *g) {
  char out[8];
  prv_render_glyph(kb, g, out, sizeof(out));
  bool cased = prv_has_case(g);
  prv_append(kb, out);
  if (cased && kb->shift == SHIFT_ONCE) kb->shift = SHIFT_OFF;  // spend explicit once
  prv_recompute_autocap(kb);
}

static void prv_commit_pending(MultitapKeyboard *kb) {
  if (!kb->pending_active) return;
  const char *eff[MAX_EFF];
  int n = prv_eff_glyphs(kb, kb->page, kb->pending_key, eff, MAX_EFF);
  if (kb->cycle_index >= n) kb->cycle_index = 0;
  kb->pending_active = false;
  if (n > 0) prv_emit_glyph(kb, eff[kb->cycle_index]);
}

static void prv_commit_timer_cb(void *data) {
  MultitapKeyboard *kb = (MultitapKeyboard *)data;
  kb->commit_timer = NULL;
  prv_commit_pending(kb);
  layer_mark_dirty(kb->layer);
}

static void prv_arm_commit_timer(MultitapKeyboard *kb) {
  int ms = kb->settings.commit_timeout_ms;
  if (ms < 150) ms = 150;
  if (kb->commit_timer) {
    if (app_timer_reschedule(kb->commit_timer, ms)) return;
    kb->commit_timer = NULL;
  }
  kb->commit_timer = app_timer_register(ms, prv_commit_timer_cb, kb);
}

static void prv_cycle_page(MultitapKeyboard *kb) {
  prv_commit_pending(kb);
  kb->page = (kb->page + 1) % PAGE_COUNT;
  layer_mark_dirty(kb->layer);
}

// ---- Feedback (blinking caret, tap flash) ------------------------------------

static void prv_caret_timer_cb(void *data) {
  MultitapKeyboard *kb = (MultitapKeyboard *)data;
  kb->caret_idle += CARET_BLINK_MS;
  if (kb->caret_idle >= CARET_IDLE_MS) {     // idle: stop the 2 Hz redraw, rest visible
    kb->caret_timer = NULL;
    if (!kb->caret_on) { kb->caret_on = true; layer_mark_dirty(kb->layer); }
    return;
  }
  kb->caret_on = !kb->caret_on;
  kb->caret_timer = app_timer_register(CARET_BLINK_MS, prv_caret_timer_cb, kb);
  layer_mark_dirty(kb->layer);
}

// Any edit wakes the caret: solid-on, idle counter reset, blink restarted if it
// had gone to sleep. The cursor stays steady while typing, then blinks during a
// brief pause, then stops redrawing the grid entirely once the user walks away.
static void prv_caret_wake(MultitapKeyboard *kb) {
  kb->caret_idle = 0;
  kb->caret_on = true;
  if (!kb->caret_timer)
    kb->caret_timer = app_timer_register(CARET_BLINK_MS, prv_caret_timer_cb, kb);
}

static void prv_flash_timer_cb(void *data) {
  MultitapKeyboard *kb = (MultitapKeyboard *)data;
  kb->flash_timer = NULL;
  kb->flash_key = -1;
  layer_mark_dirty(kb->layer);
}

static void prv_flash(MultitapKeyboard *kb, int key) {
  kb->flash_key = key;
  if (kb->flash_timer) app_timer_cancel(kb->flash_timer);
  kb->flash_timer = app_timer_register(FLASH_MS, prv_flash_timer_cb, kb);
}

static void prv_press_char_key(MultitapKeyboard *kb, int key) {
  const char *eff[MAX_EFF];
  int n = prv_eff_glyphs(kb, kb->page, key, eff, MAX_EFF);
  if (n == 0) return;
  if (n == 1) { prv_commit_pending(kb); prv_emit_glyph(kb, eff[0]); return; }

  if (kb->pending_active && kb->pending_key == key) {
    kb->cycle_index = (kb->cycle_index + 1) % n;   // re-press: advance the glyph in place
    prv_arm_commit_timer(kb);
    return;
  }
  prv_commit_pending(kb);
  // At the length cap a new key can't start a fresh char: a pending here would
  // preview (then silently drop) an over-length glyph, which reads as the last
  // letter being replaced. Cycling the active key above is still fine — it only
  // swaps the pending char in place and never grows the text.
  if (kb->max_len > 0 && (int)strlen(kb->buffer) >= kb->max_len) return;
  kb->pending_key = key; kb->cycle_index = 0; kb->pending_active = true;
  prv_arm_commit_timer(kb);
}

static void prv_do_space(MultitapKeyboard *kb) {
  prv_commit_pending(kb);
  if (kb->page == PAGE_NUMBERS) { prv_append(kb, "0"); prv_recompute_autocap(kb); return; }

  prv_append(kb, " ");
  prv_recompute_autocap(kb);
}

static void prv_press_key(MultitapKeyboard *kb, int key) {
  if (key < 0 || key >= KEY_COUNT) return;
  prv_caret_wake(kb);

  if (key == KEY_SHIFT) {
    if (kb->page != PAGE_ALPHA) {
      prv_cycle_page(kb);                  // doubles as the layout-cycle button
    } else if (kb->shift == SHIFT_OFF && kb->settings.auto_caps &&
               kb->autocap_armed && !kb->autocap_suppressed) {
      kb->autocap_suppressed = true;       // first press cancels an auto-capital
    } else {
      kb->shift = (kb->shift + 1) % 3;
    }
  } else if (key == KEY_DEL) {
    prv_do_delete(kb);
  } else if (key == KEY_SPACE) {
    prv_do_space(kb);
  } else {
    prv_press_char_key(kb, key);
  }
  layer_mark_dirty(kb->layer);
}

// ---- Hit testing ------------------------------------------------------------

// The origin that centers `content` inside `box`.
static GPoint prv_center_origin(GRect box, GSize content) {
  return (GPoint){
    .x = (int16_t)(box.origin.x + (box.size.w - content.w) / 2),
    .y = (int16_t)(box.origin.y + (box.size.h - content.h) / 2),
  };
}

// Key-grid geometry shared by drawing and hit-testing: integer cell size and the
// centered top-left origin. The grid sits GRID_VPAD below the text header and the
// same distance above the screen bottom, so the keyboard has equal white margins;
// any division remainder is split evenly L/R and T/B by the centering.
static void prv_grid_metrics(GRect b, int *cw, int *ch, GPoint *origin) {
  int top = TEXT_AREA_H + GRID_VPAD;
  int avail_h = b.size.h - GRID_VPAD - top;
  int w = b.size.w / GRID_COLS;
  int h = avail_h > 0 ? avail_h / GRID_ROWS : 0;
  *cw = w; *ch = h;
  *origin = prv_center_origin(GRect(0, top, b.size.w, avail_h),
                              GSize(w * GRID_COLS, h * GRID_ROWS));
}

static int prv_key_at_point(MultitapKeyboard *kb, GPoint p) {
  GRect b = layer_get_bounds(kb->layer);
  if (p.y < TEXT_AREA_H) return -1;
  int cw, ch; GPoint go;
  prv_grid_metrics(b, &cw, &ch, &go);
  if (cw <= 0 || ch <= 0) return -1;
  int col = (p.x - go.x) / cw, row = (p.y - go.y) / ch;
  if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) return -1;
  return row * GRID_COLS + col;
}

void multitap_keyboard_handle_tap(MultitapKeyboard *kb, GPoint point) {
  if (!kb) return;
  int key = prv_key_at_point(kb, point);
  if (key < 0) return;
  prv_flash(kb, key);
  prv_press_key(kb, key);
}

// Press feedback: highlight the key under the finger from touchdown until liftoff
// (off-grid clears it). Purely visual — the typed glyph is still decided by
// handle_tap/handle_hold on release.
void multitap_keyboard_handle_touch_down(MultitapKeyboard *kb, GPoint point) {
  if (!kb) return;
  int key = prv_key_at_point(kb, point);
  if (key == kb->touch_key) return;
  kb->touch_key = key;
  layer_mark_dirty(kb->layer);
}

void multitap_keyboard_handle_touch_up(MultitapKeyboard *kb) {
  if (!kb || kb->touch_key < 0) return;
  kb->touch_key = -1;
  layer_mark_dirty(kb->layer);
}

// Cell index under `point`, or -1 off the grid. Lets the touch driver tell when a
// finger has slid off the key it started on (to cancel the tap / long-press).
int multitap_keyboard_key_at_point(MultitapKeyboard *kb, GPoint point) {
  return kb ? prv_key_at_point(kb, point) : -1;
}

// True when `point` falls on the DEL key. The touch driver uses this to give the
// on-screen DEL key the same hold-to-repeat erase as the Down button, instead of
// the one-shot hold the other keys get.
bool multitap_keyboard_point_is_delete(MultitapKeyboard *kb, GPoint point) {
  return kb && prv_key_at_point(kb, point) == KEY_DEL;
}

// Long-press: character keys insert their phone-keypad digit (key 0..8 ->
// '1'..'9'); the space/0 key inserts the OTHER of space/0 for its page (so on
// the 123 page, holding 0 types a space); the bottom-left key cycles layout.
void multitap_keyboard_handle_hold(MultitapKeyboard *kb, GPoint point) {
  if (!kb) return;
  int key = prv_key_at_point(kb, point);
  if (key < 0) return;
  prv_caret_wake(kb);
  prv_flash(kb, key);
  if (key == KEY_SHIFT) { prv_cycle_page(kb); return; }
  if (key == KEY_DEL)   { prv_press_key(kb, KEY_DEL); return; }
  prv_commit_pending(kb);
  if (key == KEY_SPACE) prv_append(kb, (kb->page == PAGE_NUMBERS) ? " " : "0");
  else { char d[2] = { (char)('1' + key), '\0' }; prv_append(kb, d); }
  prv_recompute_autocap(kb);
  layer_mark_dirty(kb->layer);
}

// ---- Drawing ----------------------------------------------------------------

// Build the label for key `i`, drawing its glyphs in case `shift`. Callers pass
// the in-effect shift for the active multitap key and prv_next_shift() for the
// rest, so a pressed key stays capital while the others fall back to lowercase.
static void prv_key_label(MultitapKeyboard *kb, int i, int shift, char *out, size_t cap) {
  out[0] = '\0';
  if (i == KEY_SHIFT) {
    if (kb->page != PAGE_ALPHA) {                        // layout-cycle button
      strncpy(out, PAGE_IND[(kb->page + 1) % PAGE_COUNT], cap - 1);
      out[cap - 1] = '\0'; return;
    }
    const char *s = (shift == SHIFT_OFF) ? "ab" : (shift == SHIFT_ONCE) ? "Ab" : "AB";
    strncpy(out, s, cap - 1); out[cap - 1] = '\0'; return;
  }
  if (i == KEY_DEL)   { strncpy(out, "DEL", cap - 1); out[cap - 1] = '\0'; return; }
  if (i == KEY_SPACE) { strncpy(out, (kb->page == PAGE_NUMBERS) ? "0" : "space", cap - 1);
                        out[cap - 1] = '\0'; return; }
  const char *eff[MAX_EFF];
  int n = prv_eff_glyphs(kb, kb->page, i, eff, MAX_EFF);
  for (int j = 0; j < n; j++) {
    if ((unsigned char)eff[j][0] >= 0x80) continue;   // hide non-ASCII glyphs
    char gb[8]; prv_render_glyph_cased(eff[j], shift, gb, sizeof(gb));
    if (strlen(out) + strlen(gb) < cap - 1) strcat(out, gb);
  }
}

// Rendered width of text[0..off) in font `f` — i.e. the x of that byte offset on a
// left-aligned line. Cumulative-width measurement is exact (kerning included), so
// it pinpoints where a glyph/caret lands without a per-glyph layout API.
static int prv_prefix_w(const char *text, int off, GFont f) {
  char pre[BUFFER_CAP + 8];
  if (off < 0) off = 0;
  if (off > (int)sizeof(pre) - 1) off = (int)sizeof(pre) - 1;
  memcpy(pre, text, off); pre[off] = '\0';
  return graphics_text_layout_get_content_size(
           pre, f, GRect(0, 0, 2000, 200), GTextOverflowModeFill, GTextAlignmentLeft).w;
}

// Draw `text` left-aligned in `box`, picking the loosest layout that fits, in
// increasing "tightness". The candidates form one ordered ladder:
//   levels 0..N-1   : one line, shrinking through the font ladder by width;
//   levels N..2N-1  : wrap to (up to) two lines, shrinking by height;
//   level  2N       : truncate from the FRONT with a leading "..." so the most
//                     recent characters stay visible.
// Text is vertically centered.
//
// `wrap_floor` (may be NULL) adds hysteresis: the chosen level is clamped to be
// no looser than *wrap_floor, and *wrap_floor is raised to it. A tighter level
// always fits (smaller font / more lines), so clamping up never overflows. This
// stops the field from snapping one-line<->two-lines as a multitap cycles a key
// through glyphs of different widths; the caller relaxes the floor on a delete.
// `ul_start`/`ul_end` (byte offsets into `text`, or -1) mark a span to underline
// in `ul_color` — used to flag the still-cycling multi-tap glyph. `caret_at` (a
// byte offset, or -1) is where the trailing caret glyph sits in `text`, drawn in
// `caret_color`. Both effects only apply in the single-line layout, where a span's
// x-extent can be measured exactly; they're skipped once the text wraps or front-
// truncates (where positions can't be derived from the public text-layout API), so
// the caret simply stays the body color there. The caret IS still in `text` for
// layout, so wrapping accounts for it in every case.
static void prv_draw_text_fitted(GContext *ctx, const char *text, GRect box,
                                 int *wrap_floor,
                                 int ul_start, int ul_end, GColor ul_color,
                                 int caret_at, GColor caret_color) {
  // GOTHIC reserves generous padding ABOVE the caps (headroom for accents like
  // Ä/Ö), so centering the measured content box leaves the visible glyphs sitting
  // low. `rise` lifts them back to the optical middle; values were measured on the
  // emulator (28pt caps centered when lifted 5px) and scale with font size.
  static const struct { const char *key; int16_t rise; } LADDER[] = {
    { FONT_KEY_GOTHIC_28_BOLD, 5 },
    { FONT_KEY_GOTHIC_24_BOLD, 4 },
    { FONT_KEY_GOTHIC_18_BOLD, 3 },
    { FONT_KEY_GOTHIC_14_BOLD, 2 },
  };
  const int N = (int)(sizeof(LADDER) / sizeof(LADDER[0]));
  const GRect wide = GRect(0, 0, 2000, 200);            // single-line measure
  const GRect wrap = GRect(0, 0, box.size.w, 400);      // wrapped-to-width measure
  char buf[BUFFER_CAP + 16];   // fits "..." + the longest caller buffer, with room to spare

  // Find the loosest level that fits this text on its own.
  int natural = 2 * N;   // truncate, unless something looser fits first
  for (int i = 0; i < N; i++) {
    GFont f = fonts_get_system_font(LADDER[i].key);
    GSize sz = graphics_text_layout_get_content_size(
                 text, f, wide, GTextOverflowModeFill, GTextAlignmentLeft);
    if (sz.w <= box.size.w) { natural = i; break; }
  }
  if (natural == 2 * N) {
    for (int i = 0; i < N; i++) {
      GFont f = fonts_get_system_font(LADDER[i].key);
      int line_h = graphics_text_layout_get_content_size(
                     "Ag", f, wide, GTextOverflowModeFill, GTextAlignmentLeft).h;
      GSize sz = graphics_text_layout_get_content_size(
                   text, f, wrap, GTextOverflowModeWordWrap, GTextAlignmentLeft);
      if (sz.h <= line_h * 2 + 1 && sz.h <= box.size.h) { natural = N + i; break; }
    }
  }

  int level = natural;
  if (wrap_floor) {
    if (level < *wrap_floor) level = *wrap_floor;
    *wrap_floor = level;
  }

  if (level < N) {   // single line
    GFont f = fonts_get_system_font(LADDER[level].key);
    GSize sz = graphics_text_layout_get_content_size(
                 text, f, wide, GTextOverflowModeFill, GTextAlignmentLeft);
    int vy = box.origin.y + (box.size.h - sz.h) / 2 - LADDER[level].rise;
    GRect tr = GRect(box.origin.x, vy, box.size.w, sz.h + 4);

    // Body in the caller's text color, stopping before the caret so it can be
    // painted in its own color below. With no caret this is the whole string.
    char body[BUFFER_CAP + 8];
    const char *body_text = text;
    if (caret_at >= 0) {
      int c = caret_at < (int)sizeof(body) ? caret_at : (int)sizeof(body) - 1;
      memcpy(body, text, c); body[c] = '\0';
      body_text = body;
    }
    graphics_draw_text(ctx, body_text, f, tr,
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);

    // Underline the still-cycling glyph as a solid (opaque) bar spanning its
    // width, sitting just below the baseline.
    if (ul_start >= 0 && ul_end > ul_start) {
      int x0 = prv_prefix_w(text, ul_start, f);
      int x1 = prv_prefix_w(text, ul_end, f);
      int uy = vy + sz.h + 1;
      if (uy + 2 > box.origin.y + box.size.h) uy = box.origin.y + box.size.h - 2;
      if (x1 > x0) {
        graphics_context_set_fill_color(ctx, ul_color);
        graphics_fill_rect(ctx, GRect(box.origin.x + x0, uy, x1 - x0, 2), 0, GCornerNone);
      }
    }

    // Caret in its own (accent) color, painted at its measured x.
    if (caret_at >= 0) {
      int cx = prv_prefix_w(text, caret_at, f);
      graphics_context_set_text_color(ctx, caret_color);
      graphics_draw_text(ctx, text + caret_at, f,
                         GRect(box.origin.x + cx, vy, box.size.w, sz.h + 4),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    }
    return;
  }

  if (level < 2 * N) {   // wrap to at most two lines
    int i = level - N;
    GFont f = fonts_get_system_font(LADDER[i].key);
    GSize sz = graphics_text_layout_get_content_size(
                 text, f, wrap, GTextOverflowModeWordWrap, GTextAlignmentLeft);
    int vy = box.origin.y + (box.size.h - sz.h) / 2 - LADDER[i].rise;
    if (vy < box.origin.y) vy = box.origin.y;
    graphics_draw_text(ctx, text, f, GRect(box.origin.x, vy, box.size.w, sz.h + 4),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    return;
  }

  // Truncate from the front at the smallest font until two lines hold it.
  GFont f = fonts_get_system_font(LADDER[N - 1].key);
  int line_h = graphics_text_layout_get_content_size(
                 "Ag", f, wide, GTextOverflowModeFill, GTextAlignmentLeft).h;
  const char *p = text;
  while (*p) {
    p++;
    while (((unsigned char)*p & 0xC0) == 0x80) p++;
    snprintf(buf, sizeof(buf), "...%s", p);
    GSize sz = graphics_text_layout_get_content_size(
                 buf, f, wrap, GTextOverflowModeWordWrap, GTextAlignmentLeft);
    if (sz.h <= line_h * 2 + 1 && sz.h <= box.size.h) break;
  }
  snprintf(buf, sizeof(buf), "...%s", p);
  GSize sz = graphics_text_layout_get_content_size(
               buf, f, wrap, GTextOverflowModeWordWrap, GTextAlignmentLeft);
  int vy = box.origin.y + (box.size.h - sz.h) / 2 - LADDER[N - 1].rise;
  if (vy < box.origin.y) vy = box.origin.y;
  graphics_draw_text(ctx, buf, f, GRect(box.origin.x, vy, box.size.w, sz.h + 4),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

// Recolor a palettized icon so its opaque pixels become `ink` while transparent
// palette entries stay clear. Lets one arrow asset render in any theme/state
// color. No-op if the asset didn't compile to a palettized format.
static void prv_tint_icon(GBitmap *bmp, GColor ink) {
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

// Draw a ⎵ space symbol (a horizontal bar with short upturned ends), centered
// in `face` and stroked in `ink`.
static void prv_draw_space_symbol(GContext *ctx, GRect face, GColor ink) {
  int uw = face.size.w / 2, ut = 6;
  int ux = face.origin.x + (face.size.w - uw) / 2;
  int uy = face.origin.y + face.size.h * 3 / 5;
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(ux, uy), GPoint(ux + uw, uy));
  graphics_draw_line(ctx, GPoint(ux, uy), GPoint(ux, uy - ut));
  graphics_draw_line(ctx, GPoint(ux + uw, uy), GPoint(ux + uw, uy - ut));
  graphics_context_set_stroke_width(ctx, 1);
}

// Backspace (⌫) glyph for the DEL key: a left-pointing tag filled in `ink`, with
// an × knocked back out in `knockout` (the key's own fill) so the cut reads
// through to the surface. Shape adapted from the freakified pebble-calculator
// (MIT). Centered on the face, nudged 2px left so the point isn't crowded.
static GPath  *s_bksp_path = NULL;
static GPathInfo s_bksp_info = {
  .num_points = 5,
  .points = (GPoint[]){ {-10, 0}, {-3, -7}, {12, -7}, {12, 7}, {-3, 7} },
};
static void prv_draw_backspace(GContext *ctx, GRect face, GColor ink, GColor knockout) {
  GPoint c = GPoint(face.origin.x + face.size.w / 2 - 1,
                    face.origin.y + face.size.h / 2);
  if (!s_bksp_path) s_bksp_path = gpath_create(&s_bksp_info);
  gpath_move_to(s_bksp_path, c);
  graphics_context_set_fill_color(ctx, ink);
  gpath_draw_filled(ctx, s_bksp_path);
  graphics_context_set_stroke_color(ctx, knockout);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(c.x + 1, c.y - 3), GPoint(c.x + 8, c.y + 3));
  graphics_draw_line(ctx, GPoint(c.x + 1, c.y + 3), GPoint(c.x + 8, c.y - 3));
  graphics_context_set_stroke_width(ctx, 1);
}

// One step toward black per channel — the shade a key darkens to on press.
static GColor prv_darker(GColor c) {
  c.r = c.r ? c.r - 1 : 0;
  c.g = c.g ? c.g - 1 : 0;
  c.b = c.b ? c.b - 1 : 0;
  return c;
}

// Resolve a keyboard's selected index to a theme, with the app slot at
// THEME_COUNT and any out-of-range index falling back to the first built-in.
static const Theme *prv_theme(const MultitapKeyboard *kb) {
  if (s_has_app_theme && kb->theme == THEME_COUNT) return &s_app_theme;
  int i = (kb->theme < 0 || kb->theme >= THEME_COUNT) ? 0 : kb->theme;
  return &THEMES[i];
}

static void prv_update_proc(Layer *layer, GContext *ctx) {
  MultitapKeyboard *kb = *(MultitapKeyboard **)layer_get_data(layer);
  GRect b = layer_get_bounds(layer);

  const Theme *th = prv_theme(kb);
  GColor c_bg  = (GColor){ .argb = th->bg };
  GColor c_fg  = (GColor){ .argb = th->fg };
  GColor c_key = (GColor){ .argb = th->key };
  GColor c_acc = (GColor){ .argb = th->accent };
  GColor c_act = (GColor){ .argb = th->accent_text };
  // Surface model: the screen is the soft tinted shade, while the text field and
  // the letter keys are the bright surface that floats on it. We reuse the
  // existing palette for this — `key` (the soft shade) backs the screen, `bg`
  // (the bright shade) becomes the field + letter keys — so no new theme color
  // is needed and every theme keeps its character.
  graphics_context_set_fill_color(ctx, c_bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  GFont key_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  char shown[BUFFER_CAP + 8];
  strncpy(shown, kb->buffer, sizeof(shown)); shown[sizeof(shown) - 1] = '\0';
  // Byte span of the still-cycling pending glyph within `shown`, underlined below
  // to flag it as uncommitted (the classic multi-tap cue). -1 = nothing pending.
  int ul_start = -1, ul_end = -1;
  if (kb->pending_active) {
    const char *eff[MAX_EFF];
    int n = prv_eff_glyphs(kb, kb->page, kb->pending_key, eff, MAX_EFF);
    if (kb->cycle_index < n) {
      char gb[8];
      prv_render_glyph(kb, eff[kb->cycle_index], gb, sizeof(gb));
      if (strlen(shown) + strlen(gb) < sizeof(shown)) {
        ul_start = (int)strlen(shown);
        strcat(shown, gb);
        ul_end = (int)strlen(shown);
      }
    }
  }
  int caret_at = -1;
  if (kb->caret_on && strlen(shown) + 1 < sizeof(shown)) {
    caret_at = (int)strlen(shown);
    strcat(shown, "|");
  }

  // The text header is a white section (standing in for the old divider),
  // holding a plain field outlined with a 3px frame (matching the duration
  // dial boxes) where the text is drawn.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, TEXT_AREA_H), 0, GCornerNone);

  GRect field = GRect(4, 4, b.size.w - 8, TEXT_AREA_H - 8);
  graphics_context_set_stroke_color(ctx, c_fg);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_rect(ctx, field);

  // Relax the sticky layout only when the committed text actually shrank (a
  // delete). Track kb->buffer, not `shown`: the blinking caret toggles shown's
  // length every frame, and a multitap holds buffer length constant while the
  // pending glyph cycles — exactly when we want the layout to stay put.
  int cur_len = (int)strlen(kb->buffer);
  if (cur_len < kb->last_text_len) kb->wrap_floor = 0;
  kb->last_text_len = cur_len;

  graphics_context_set_text_color(ctx, c_fg);
  prv_draw_text_fitted(ctx, shown, GRect(field.origin.x + 6, field.origin.y,
                                         field.size.w - 12, field.size.h),
                       &kb->wrap_floor, ul_start, ul_end, c_acc, caret_at, c_acc);

  // Keys are filled, rounded, and gapped like the throw grid: letter keys rest
  // on the soft `key` fill; the bottom function row (Shift/space/DEL) is a bold
  // accent strip — the keyboard's MISS bar. A press deepens a key for feedback:
  // each key darkens its own resting fill (keeping its label color) so a tap
  // reads as "pushed in" without changing hue. Hit testing still uses the full
  // cell (prv_key_at_point), so the gaps stay tappable.
  // Cell size + centered origin (shared with prv_key_at_point so taps land on the
  // drawn cells). GRID_VPAD above and below gives the keyboard equal white margins.
  int cw, ch; GPoint go;
  prv_grid_metrics(b, &cw, &ch, &go);
  // The active multitap key keeps the in-effect case while it cycles; every other
  // key (incl. Shift) shows the case the next press will use — so a one-shot/auto
  // capital visibly "spends" on the pressed key and the rest drop to lowercase.
  int eff_shift = prv_effective_shift(kb);
  int next_shift = prv_next_shift(kb);
  for (int i = 0; i < KEY_COUNT; i++) {
    int col = i % GRID_COLS, row = i / GRID_COLS;
    GRect face = GRect(go.x + col * cw + 2, go.y + row * ch + 3, cw - 4, ch - 4);

    bool is_active = (kb->pending_active && kb->pending_key == i);
    int  key_shift = is_active ? eff_shift : next_shift;
    bool is_function = (i == KEY_SHIFT || i == KEY_SPACE || i == KEY_DEL);
    bool shift_on = (i == KEY_SHIFT && kb->page == PAGE_ALPHA &&
                     key_shift != SHIFT_OFF);
    bool pressed = (i == kb->touch_key) || (i == kb->flash_key) ||
                   (kb->pending_active && kb->pending_key == i) || shift_on;

    GColor fill, txt;
    if (i == KEY_SHIFT || i == KEY_DEL) {
      fill = GColorDarkGray;
      txt  = GColorWhite;
    } else if (i == KEY_SPACE) {
      fill = GColorLightGray;
      txt  = GColorBlack;
    } else if (is_function) {
      fill = c_acc;    // accent strip
      txt  = c_act;
    } else {
      fill = c_key;    // soft letter
      txt  = c_fg;
    }
    GColor sink = prv_darker(fill);
    GColor draw_fill = pressed ? sink : fill;
    graphics_context_set_fill_color(ctx, draw_fill);
    graphics_fill_rect(ctx, face, 4, GCornersAll);
    graphics_context_set_text_color(ctx, txt);

    if (i == KEY_SHIFT && kb->page == PAGE_ALPHA) {
      // Shift state as an arrow icon: off (plain), once (dash), lock (dash
      // solid), recolored to the key's current text color.
      GBitmap *icon = kb->icon_shift[key_shift];
      if (icon) {
        prv_tint_icon(icon, txt);
        GSize is = gbitmap_get_bounds(icon).size;
        // +2px: the glyph sits high in its bounds, so a geometric center reads
        // a few pixels too high — nudge down to optically center.
        GRect ir = { .origin = prv_center_origin(face, is), .size = is };
        ir.origin.y += 2;
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, icon, ir);
      }
    } else if (i == KEY_SPACE && kb->page != PAGE_NUMBERS) {
      prv_draw_space_symbol(ctx, face, txt);
    } else if (i == KEY_DEL) {
      // Backspace glyph in place of a "DEL" label.
      prv_draw_backspace(ctx, face, txt, draw_fill);
    } else {
      char lbl[LABEL_CAP]; prv_key_label(kb, i, key_shift, lbl, sizeof(lbl));
      // GOTHIC_18 pads above the caps, so center on its cap box (22) and nudge
      // up 2px to optically center — same correction the menu UI uses.
      GRect tr = GRect(face.origin.x + 2, face.origin.y + (face.size.h - 22) / 2, face.size.w - 2, 22);
      graphics_draw_text(ctx, lbl, key_font, tr,
                         GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    }
  }
}

// ---- Lifecycle --------------------------------------------------------------

MultitapKeyboard *multitap_keyboard_create(GRect frame, MultitapKeyboardDoneHandler done_handler,
                               void *context) {
  MultitapKeyboard *kb = malloc(sizeof(MultitapKeyboard));
  if (!kb) return NULL;
  memset(kb, 0, sizeof(MultitapKeyboard));
  kb->done_handler = done_handler;
  kb->context = context;
  kb->page = PAGE_ALPHA;
  kb->shift = SHIFT_OFF;
  kb->settings.commit_timeout_ms = MULTITAP_DEFAULT_WAIT_MS;
  kb->settings.auto_caps = true;
  for (int i = 0; i < EXT_COUNT; i++) kb->ext_enabled[i] = true;
  kb->theme = 0;
  kb->flash_key = -1;
  kb->touch_key = -1;
  kb->caret_on = true;
  kb->buffer[0] = '\0';
  prv_recompute_autocap(kb);

  kb->layer = layer_create_with_data(frame, sizeof(MultitapKeyboard *));
  if (!kb->layer) { free(kb); return NULL; }
  *(MultitapKeyboard **)layer_get_data(kb->layer) = kb;
  layer_set_update_proc(kb->layer, prv_update_proc);
  kb->icon_shift[SHIFT_OFF]  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SHIFT_OFF);
  kb->icon_shift[SHIFT_ONCE] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SHIFT_ONCE);
  kb->icon_shift[SHIFT_LOCK] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SHIFT_LOCK);
  kb->caret_timer = app_timer_register(CARET_BLINK_MS, prv_caret_timer_cb, kb);
  return kb;
}

void multitap_keyboard_destroy(MultitapKeyboard *kb) {
  if (!kb) return;
  if (kb->commit_timer) { app_timer_cancel(kb->commit_timer); kb->commit_timer = NULL; }
  if (kb->caret_timer)  { app_timer_cancel(kb->caret_timer);  kb->caret_timer = NULL; }
  if (kb->flash_timer)  { app_timer_cancel(kb->flash_timer);  kb->flash_timer = NULL; }
  for (int i = 0; i < 3; i++) if (kb->icon_shift[i]) gbitmap_destroy(kb->icon_shift[i]);
  if (kb->layer) layer_destroy(kb->layer);
  if (s_bksp_path) { gpath_destroy(s_bksp_path); s_bksp_path = NULL; }   // lazily created per session
  free(kb);
}

Layer *multitap_keyboard_get_layer(MultitapKeyboard *kb) { return kb ? kb->layer : NULL; }
const char *multitap_keyboard_get_text(MultitapKeyboard *kb) { return kb ? kb->buffer : ""; }

// Trim the buffer to at most `max` bytes without splitting a UTF-8 glyph.
static void prv_clamp_to_max(MultitapKeyboard *kb) {
  if (kb->max_len <= 0) return;
  size_t i = strlen(kb->buffer);
  if (i <= (size_t)kb->max_len) return;
  i = (size_t)kb->max_len;
  while (i > 0 && ((unsigned char)kb->buffer[i] & 0xC0) == 0x80) i--;  // drop continuation
  kb->buffer[i] = '\0';
}

void multitap_keyboard_set_text(MultitapKeyboard *kb, const char *text) {
  if (!kb) return;
  if (kb->commit_timer) { app_timer_cancel(kb->commit_timer); kb->commit_timer = NULL; }
  kb->pending_active = false;
  if (text) { strncpy(kb->buffer, text, BUFFER_CAP - 1); kb->buffer[BUFFER_CAP - 1] = '\0'; }
  else kb->buffer[0] = '\0';
  prv_clamp_to_max(kb);
  prv_recompute_autocap(kb);
  layer_mark_dirty(kb->layer);
}

void multitap_keyboard_set_max_len(MultitapKeyboard *kb, int max_bytes) {
  if (!kb) return;
  kb->max_len = (max_bytes > 0 && max_bytes < BUFFER_CAP) ? max_bytes : 0;
  prv_clamp_to_max(kb);
  layer_mark_dirty(kb->layer);
}

void multitap_keyboard_get_settings(MultitapKeyboard *kb, MultitapSettings *out) {
  if (kb && out) *out = kb->settings;
}

void multitap_keyboard_set_settings(MultitapKeyboard *kb, const MultitapSettings *s) {
  if (!kb || !s) return;
  kb->settings = *s;
  if (kb->settings.commit_timeout_ms < 150) kb->settings.commit_timeout_ms = 150;
  prv_recompute_autocap(kb);
  layer_mark_dirty(kb->layer);
}

// ---- Button-shortcut actions ------------------------------------------------
// Driven by the physical watch buttons, as opposed to on-screen key taps.

void multitap_keyboard_backspace(MultitapKeyboard *kb) { if (kb) prv_press_key(kb, KEY_DEL); }

void multitap_keyboard_submit(MultitapKeyboard *kb) {
  if (!kb) return;
  prv_commit_pending(kb);
  layer_mark_dirty(kb->layer);
  // Fire last: the handler may pop/destroy the keyboard, so nothing may touch
  // `kb` after this. `buffer` stays valid for the duration of the callback.
  if (kb->done_handler) kb->done_handler(kb->buffer, kb->context);
}

void multitap_keyboard_cycle_page(MultitapKeyboard *kb) { if (kb) prv_cycle_page(kb); }

void multitap_keyboard_newline(MultitapKeyboard *kb) {
  if (!kb) return;
  prv_caret_wake(kb);
  prv_commit_pending(kb);
  prv_append(kb, "\n");
  prv_recompute_autocap(kb);
  layer_mark_dirty(kb->layer);
}

int multitap_keyboard_delete_repeat_ms(MultitapKeyboard *kb) {
  return 100;
}

// ---- Extended (toggleable) characters ---------------------------------------

int multitap_keyboard_ext_count(void) { return EXT_COUNT; }

const char *multitap_keyboard_ext_glyph(int index) {
  return (index >= 0 && index < EXT_COUNT) ? EXT_DEFS[index].glyph : "";
}

bool multitap_keyboard_ext_enabled(MultitapKeyboard *kb, int index) {
  return (kb && index >= 0 && index < EXT_COUNT) ? kb->ext_enabled[index] : false;
}

void multitap_keyboard_ext_set_enabled(MultitapKeyboard *kb, int index, bool enabled) {
  if (!kb || index < 0 || index >= EXT_COUNT) return;
  kb->ext_enabled[index] = enabled;
  kb->pending_active = false;   // cycle indices may have shifted
  layer_mark_dirty(kb->layer);
}

// ---- Themes -----------------------------------------------------------------

int multitap_keyboard_theme_count(void) { return THEME_COUNT; }   // built-ins only

const char *multitap_keyboard_theme_name(int index) {
  if (s_has_app_theme && index == THEME_COUNT) return s_app_theme.name;
  return (index >= 0 && index < THEME_COUNT) ? THEMES[index].name : "";
}

int multitap_keyboard_get_theme(MultitapKeyboard *kb) { return kb ? kb->theme : 0; }

void multitap_keyboard_set_theme(MultitapKeyboard *kb, int index) {
  int max = s_has_app_theme ? THEME_COUNT : THEME_COUNT - 1;   // app slot is last
  if (!kb || index < 0 || index > max) return;
  kb->theme = index;
  layer_mark_dirty(kb->layer);
}

UiTheme multitap_keyboard_get_theme_colors(MultitapKeyboard *kb) {
  const Theme *th = kb ? prv_theme(kb) : &THEMES[0];
  return (UiTheme){
    .background    = (GColor){ .argb = th->bg },
    .text          = (GColor){ .argb = th->fg },
    .text_muted    = (GColor){ .argb = th->fg },   // keyboard palette has no muted role
    .neutral       = (GColor){ .argb = th->key },
    .accent        = (GColor){ .argb = th->accent },
    .accent_text   = (GColor){ .argb = th->accent_text },
    .danger        = (GColor){ .argb = th->danger },
    .danger_light  = (GColor){ .argb = th->danger_light },
    .danger_darker = (GColor){ .argb = th->danger_darker },
  };
}

void multitap_keyboard_set_app_theme(const char *name, UiTheme t) {
  if (!name) { s_has_app_theme = false; return; }
  s_app_theme.name        = name;
  s_app_theme.bg          = t.background.argb;
  s_app_theme.fg          = t.text.argb;
  s_app_theme.key         = t.neutral.argb;        // ui has no "key" role → neutral
  s_app_theme.accent      = t.accent.argb;
  s_app_theme.accent_text = t.accent_text.argb;
  // The DEL key always uses this fixed danger ramp (not the caller's t.danger*
  // fields) — the same triple THEMES[] uses for every built-in, so the
  // destructive key reads the same no matter which skin is active.
  s_app_theme.danger        = GColorDarkCandyAppleRedARGB8;  // key fill
  s_app_theme.danger_light  = GColorMelonARGB8;              // glyph ink
  s_app_theme.danger_darker = GColorBulgarianRoseARGB8;      // shadow / press
  s_has_app_theme = true;
}

bool multitap_keyboard_has_app_theme(void) { return s_has_app_theme; }
int  multitap_keyboard_app_theme_index(void) { return s_has_app_theme ? THEME_COUNT : -1; }
