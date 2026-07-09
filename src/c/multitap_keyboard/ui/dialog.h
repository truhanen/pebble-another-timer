#pragma once
#include <pebble.h>
#include "button.h"

// Modal prompt with optional title/body text and themed buttons.
// Strings are copied. Button callbacks run after the dialog has dismissed.
// Buttons use Up/Down + Select only; touch is disabled for confirms.

#define DIALOG_MAX_BUTTONS 3

typedef struct {
  const char    *label;                 // button text (required)
  UiButtonScheme scheme;                // PRIMARY (default 0) / DANGER / NEUTRAL
  void         (*on_click)(void *ctx);  // NULL → just dismiss the dialog
} DialogButton;

typedef struct {
  const char  *title;                   // bold heading (NULL/"" → omitted)
  const char  *text;                    // body paragraph (NULL/"" → omitted)
  DialogButton buttons[DIALOG_MAX_BUTTONS];
  uint8_t      button_count;            // 1 .. DIALOG_MAX_BUTTONS
  void        *ctx;                     // passed to every on_click
} DialogConfig;

// Push a modal dialog. The dialog owns its lifecycle and frees itself on pop.
Window *dialog_push(DialogConfig cfg);

// Two-button confirm. NULL labels fall back to "OK" / "Cancel".
Window *dialog_confirm_push(const char *title, const char *text,
                            const char *confirm_label, UiButtonScheme confirm_scheme,
                            const char *cancel_label,
                            void (*on_confirm)(void *ctx), void *ctx);
