#pragma once
#include <pebble.h>
#include "button.h"

// Button container with shared callbacks for touch and physical buttons.
// Touch taps/holds/slides; Up/Down moves focus and Select activates it.
// on_click/on_long_press are deferred, so handlers may navigate.

#define UI_BTN_GROUP_MAX 16   // buttons per group (the throw grid uses 13)

typedef enum {
  UI_BTN_TAP,
  UI_BTN_HOLD,
  UI_BTN_REPEAT,
} UiButtonBehavior;

typedef struct {
  int              id;            // identifier passed back to the handlers
  GRect            frame;         // hit region + draw box (group-layer coords)
  UiButtonSpec     look;          // base appearance (see button.h)
  UiButtonStyle    active_style;  // emphasis when focused/pressed (default SOLID)
  UiButtonScheme   active_scheme; // emphasis scheme             (default PRIMARY)
  GColor           active_fill;   // non-clear overrides active fill
  GColor           active_ink;    // non-clear overrides active ink
  UiButtonBehavior behavior;      // TAP (default) / HOLD / REPEAT
  uint16_t         repeat_ms;     // REPEAT interval (0 => a sane default)
  bool             no_touch;      // exclude from touch hit-testing
  bool             no_focus;      // exclude from the physical focus ring
} UiButton;

typedef struct {
  void (*on_click)(int id, void *ctx);       // tap / focused Select / repeat tick
  void (*on_long_press)(int id, void *ctx);  // HOLD behavior only
  void (*on_press)(int id, void *ctx);       // immediate: a press began (feedback)
  void (*on_release)(int id, void *ctx);     // immediate: a press ended (feedback)
} UiButtonGroupHandlers;

typedef struct UiButtonGroup UiButtonGroup;

// Create against a window; installs click config and touch handling.
UiButtonGroup *ui_button_group_create(Window *window, GRect frame,
                                      UiButtonGroupHandlers handlers, void *ctx);

// Create in an existing input owner; feed events with handle_* below.
UiButtonGroup *ui_button_group_create_in_layer(Layer *parent, GRect frame,
                                               UiButtonGroupHandlers handlers, void *ctx);

// Register a button (copied); returns its index, or -1 if the group is full.
int    ui_button_group_add(UiButtonGroup *g, UiButton btn);

Layer *ui_button_group_get_layer(UiButtonGroup *g);
void   ui_button_group_destroy(UiButtonGroup *g);

// ---- fed mode (composition; a host owns the touch service / click config) ----
// Forward a touch event. Coordinates are screen-space (TouchEvent.x/y); the
// group maps them into its own layer.
void   ui_button_group_handle_touch(UiButtonGroup *g, const TouchEvent *event);
void   ui_button_group_focus_prev(UiButtonGroup *g);   // Up
void   ui_button_group_focus_next(UiButtonGroup *g);   // Down
void   ui_button_group_press_focused(UiButtonGroup *g);    // Select down
void   ui_button_group_release_focused(UiButtonGroup *g);  // Select up
