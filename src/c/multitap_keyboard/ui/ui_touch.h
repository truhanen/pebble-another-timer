#pragma once
#include <pebble.h>

// =============================================================================
// ui_touch — one shared subscription to the app-global touch service.
//
// Pebble holds a single touch handler per app, so independent widgets (a button
// grid, the keyboard) must not subscribe directly: creating a second widget
// would silently steal the first's events, and destroying it would strand the
// survivor with no touch at all. Widgets register here instead; the service
// stays subscribed while any client remains, and every event is fanned out to
// all of them. Clients decide relevance themselves — typically by acting only
// while their own window is on top of the stack (see the callers).
// =============================================================================

typedef void (*UiTouchFn)(const TouchEvent *event, void *ctx);

// Register a client. Returns false when the watch has no touch support (or the
// client table is full); registering the same fn/ctx pair twice is a no-op.
bool ui_touch_register(UiTouchFn fn, void *ctx);
void ui_touch_unregister(UiTouchFn fn, void *ctx);
