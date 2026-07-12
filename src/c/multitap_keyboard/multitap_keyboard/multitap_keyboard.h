#pragma once
#include <pebble.h>

// =============================================================================
// multitap_keyboard — a Nokia-style multi-tap on-screen keyboard for PebbleOS
// touchscreen watches (Pebble Time 2 / Round 2, PebbleOS 4.9.171+).
//
// Tap a key to cycle through its characters (a -> b -> c -> å -> ä -> æ).
// Tapping the SAME key again before the commit timeout advances the character;
// tapping a DIFFERENT key or letting the timeout elapse commits it.
//
// Layout: a 3x4 grid. Keys 0-8 are the character keys; the bottom row is
// Shift (bottom-left), space, and DEL. There is no on-screen OK — submit is a
// host action (multitap_keyboard_submit, e.g. wired to the Select button).
//
// Shift cycles off -> next-letter -> caps-lock, for ASCII a-z plus the Nordic
// letters å ä ö æ ø (-> Å Ä Ö Æ Ø). Those extended letters are hidden from the
// key labels (so `abc` reads "abc") but still reachable by cycling, and can be
// turned on/off individually via the extended-character API.
//
// Behavior settings (see MultitapSettings): the multi-tap wait time and
// auto-capitalize at the start of sentences (the shift indicator reflects it).
// Text is stored as UTF-8.
//
// This is the keyboard WIDGET (a Layer). It takes no touch dependency — feed
// it taps via multitap_keyboard_handle_tap(), so it stays portable and testable.
// For the easy, full-screen drop-in (touch + buttons + return-to-app already
// wired), use multitap_keyboard_window_push() from multitap_keyboard_window.h instead.
// =============================================================================

#define MULTITAP_DEFAULT_WAIT_MS 800

// Tunable behaviors, surfaced by the on-watch settings menu.
typedef struct {
  int  commit_timeout_ms;  // wait between presses of the same key before commit
  bool auto_caps;          // capitalize first letter of each sentence
} MultitapSettings;

// Fired by multitap_keyboard_submit(). `text` is only valid during the callback.
typedef void (*MultitapKeyboardDoneHandler)(const char *text, void *context);

typedef struct MultitapKeyboard MultitapKeyboard;

MultitapKeyboard *multitap_keyboard_create(GRect frame, MultitapKeyboardDoneHandler done_handler,
                               void *context);
void  multitap_keyboard_destroy(MultitapKeyboard *kb);
Layer *multitap_keyboard_get_layer(MultitapKeyboard *kb);

// Single entry point for touch input (layer-local coordinates).
void multitap_keyboard_handle_tap(MultitapKeyboard *kb, GPoint point);

// Press feedback while a finger is down, independent of the tap/hold action that
// fires on liftoff. The touch driver calls _touch_down on touchdown so the key
// under the finger draws pressed immediately, and _touch_up on liftoff to release
// it. A point off the grid clears the highlight. Purely visual: neither types nor
// commits anything (the typed glyph is still decided by handle_tap/handle_hold).
void multitap_keyboard_handle_touch_down(MultitapKeyboard *kb, GPoint point);
void multitap_keyboard_handle_touch_up(MultitapKeyboard *kb);

// Cell index under `point`, or -1 off the grid. The touch driver compares this to
// the touchdown cell to cancel the tap/long-press once a finger slides off it.
int multitap_keyboard_key_at_point(MultitapKeyboard *kb, GPoint point);

// Long-press at a point: char keys / space insert their keypad digit
// (key 0..8 -> 1..9, space -> 0); the bottom-left key cycles the layout.
// DEL is the exception: the touch driver should NOT route a DEL hold here (that
// would erase just once). Use multitap_keyboard_point_is_delete() to detect it and
// drive a repeating multitap_keyboard_backspace() instead — see multitap_keyboard_window.c.
void multitap_keyboard_handle_hold(MultitapKeyboard *kb, GPoint point);

// True when `point` lands on the DEL key, so the touch driver can give it
// hold-to-repeat erase (like the Down button) rather than a one-shot hold.
bool multitap_keyboard_point_is_delete(MultitapKeyboard *kb, GPoint point);

// Programmatic actions — handy for wiring physical buttons.
void multitap_keyboard_backspace(MultitapKeyboard *kb);
void multitap_keyboard_submit(MultitapKeyboard *kb);
void multitap_keyboard_cycle_page(MultitapKeyboard *kb);
void multitap_keyboard_newline(MultitapKeyboard *kb);   // insert a '\n' (e.g. hold Select)

// Current hold-to-repeat delete interval (ms) for the active delete mode —
// wire this to the Down button's repeating-click subscription.
int multitap_keyboard_delete_repeat_ms(MultitapKeyboard *kb);

const char *multitap_keyboard_get_text(MultitapKeyboard *kb);
void multitap_keyboard_set_text(MultitapKeyboard *kb, const char *text);

// Cap the entered text at `max_bytes` (excluding the NUL), e.g. to fit a fixed
// storage field. Keys that would overflow the cap are ignored; set_text()
// truncates to it. Counts bytes, not characters, so it never splits a UTF-8
// glyph. Pass 0 (the default) for no limit beyond the internal buffer.
void multitap_keyboard_set_max_len(MultitapKeyboard *kb, int max_bytes);

// Behavior settings.
void multitap_keyboard_get_settings(MultitapKeyboard *kb, MultitapSettings *out);
void multitap_keyboard_set_settings(MultitapKeyboard *kb, const MultitapSettings *settings);

// Toggleable extended (non-ASCII) characters (e.g. å ä ö æ ø). These are
// hidden from the key labels but appended to the relevant key's cycle when
// enabled. The registry is static (shared); the on/off state is per-keyboard
// and all-on by default.
int  multitap_keyboard_ext_count(void);
const char *multitap_keyboard_ext_glyph(int index);
bool multitap_keyboard_ext_enabled(MultitapKeyboard *kb, int index);
void multitap_keyboard_ext_set_enabled(MultitapKeyboard *kb, int index, bool enabled);

// Color themes (Light, Dark, Ocean, ...). The registry is static; the selected
// index is per-keyboard. multitap_keyboard_theme_count() counts the BUILT-IN themes
// only; an app-supplied theme (below) lives at index == count.
int  multitap_keyboard_theme_count(void);
const char *multitap_keyboard_theme_name(int index);
int  multitap_keyboard_get_theme(MultitapKeyboard *kb);
void multitap_keyboard_set_theme(MultitapKeyboard *kb, int index);

// A themeable palette, shared between the keyboard and a host surface (e.g. the
// on-watch settings menu) that wants to paint itself to match. Keep each fg/bg
// pair legible against each other: text on background, accent_text on accent,
// and danger_light behind danger.
typedef struct {
  GColor background;     // window / menu fill
  GColor text;           // primary ink
  GColor text_muted;     // secondary / disabled ink
  GColor neutral;        // soft low-contrast surface fill
  GColor accent;         // selection highlight
  GColor accent_text;    // ink drawn on the accent
  GColor danger;         // destructive / negative signal (the DEL key)
  GColor danger_light;   // soft danger surface fill (danger ink reads on it)
  GColor danger_darker;  // pressed / drop-shadow shade of a danger surface
} UiTheme;

// The colors of the keyboard's CURRENTLY-ACTIVE theme, packed as a UiTheme so a
// host surface (e.g. the on-watch settings menu) can paint itself to match the
// live keyboard skin. Reflects whatever is actually applied — built-in pick or
// app skin — not merely an index. The keyboard's "key" fill maps to `neutral`;
// it has no muted-ink role, so `text_muted` mirrors `text`.
UiTheme multitap_keyboard_get_theme_colors(MultitapKeyboard *kb);

// Register the app-brand keyboard skin from your app's shared UiTheme, so the
// brand colors live in ONE place instead of being duplicated here. It appends a
// single pickable theme after the built-ins, addressed as theme index
// multitap_keyboard_theme_count(). The mapping onto the keyboard's roles:
//   background -> screen      accent      -> function row / active keys
//   text       -> ink/divider accent_text -> ink on the accent
//   neutral    -> resting key fill        danger/danger_light -> the DEL key
// The keyboard has no "key fill" token of its own, so `neutral` plays that role.
// `name` shows in the picker and must outlive the keyboard (use a string
// literal). Call once at startup, before any keyboard
// window is shown; pass name=NULL to clear the slot.
void multitap_keyboard_set_app_theme(const char *name, UiTheme theme);
bool multitap_keyboard_has_app_theme(void);
int  multitap_keyboard_app_theme_index(void);   // index for set_theme(), or -1 if none
