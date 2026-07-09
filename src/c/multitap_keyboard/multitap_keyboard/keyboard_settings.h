#pragma once
#include <pebble.h>
#include "multitap_keyboard.h"

// The keyboard's persisted settings occupy 12 consecutive keys, at
// MULTITAP_SETTINGS_BASE_KEY .. +11, in the HOST APP's persist namespace
// (Pebble has no per-library storage). If your app uses low persist keys,
// relocate the block with a compiler define (-DMULTITAP_SETTINGS_BASE_KEY=…);
// changing it on a shipped app orphans previously saved keyboard settings.
#ifndef MULTITAP_SETTINGS_BASE_KEY
#define MULTITAP_SETTINGS_BASE_KEY 400
#endif

// Load persisted settings (or defaults) and apply them to the keyboard.
// Call once at startup, after the keyboard is created.
void settings_load_and_apply(MultitapKeyboard *kb);

// Push the on-watch settings menu. Wire this to a long-press of the Up button.
// Changes are applied to `kb` live and persisted; Back returns to the keyboard.
void settings_window_push(MultitapKeyboard *kb);
