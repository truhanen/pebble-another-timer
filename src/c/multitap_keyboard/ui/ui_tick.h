#pragma once
#include <pebble.h>

// =============================================================================
// ui_tick — one shared MINUTE_UNIT subscription for the whole UI lib.
//
// Pebble's tick timer service holds a single handler per app, so independent
// widgets (headers, footers, future clocks) must not subscribe directly: the
// last one to subscribe silently steals the tick from the others, and the first
// one to unsubscribe strands the survivors with frozen clocks. Widgets register
// a callback here instead; the service stays subscribed while any client
// remains, and every minute tick is fanned out to all of them.
// =============================================================================

typedef void (*UiTickFn)(void);

// Register `fn` for minute ticks. Registering the same fn twice is a no-op.
void ui_tick_register(UiTickFn fn);
void ui_tick_unregister(UiTickFn fn);
