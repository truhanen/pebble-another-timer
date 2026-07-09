#pragma once
#include <pebble.h>

// =============================================================================
// multitap_keyboard_window — a full-screen modal text-entry UI built on the
// multitap_keyboard widget. This is the easy "drop-in" entry point: one call pushes
// a keyboard that owns the whole screen, wires up touch and the physical
// buttons, applies saved settings, and hands the typed text back to your app.
//
// Typical use, from anywhere in your app:
//
//     static void on_text(const char *text, void *ctx) {
//       if (text) {                 // OK pressed
//         // copy `text` (only valid during this call) and use it
//       } else {                    // Back pressed (cancelled)
//       }
//     }
//     ...
//     multitap_keyboard_window_push(on_text, "optional starting text", NULL);
//
// Buttons inside the keyboard: OK (Select) confirms, Back cancels, Up cycles
// the layout (hold Up for Settings), hold OK for a newline, Down backspaces.
//
// Single instance: only one keyboard window is shown at a time. Depends on
// multitap_keyboard.{c,h} and settings_window.{c,h}.
// =============================================================================

// Result callback. On OK, `text` is the entered string (valid ONLY during this
// call — copy it if you need to keep it). On Back/cancel, `text` is NULL. The
// keyboard window removes itself right after this returns, so the handler may
// freely push a new window (it will stay) or navigate.
typedef void (*MultitapKeyboardResultHandler)(const char *text, void *context);

// Push the full-screen keyboard. `initial_text` pre-fills the field (pass NULL
// for an empty field) and is copied internally, so it need not outlive the call.
// `context` is passed back to your handler untouched.
void multitap_keyboard_window_push(MultitapKeyboardResultHandler handler,
                             const char *initial_text, void *context);

// As above, but caps the entry at `max_len` bytes (excluding the NUL) — e.g.
// to fit a fixed-size name field. Pass 0 for no limit (same as the call above).
void multitap_keyboard_window_push_ex(MultitapKeyboardResultHandler handler,
                                const char *initial_text, int max_len,
                                void *context);
