#pragma once
#include <pebble.h>

// =============================================================================
// header — a status bar pinned to the TOP of a window: a page title on the left
// and an optional live clock on the right (following the watch's 12/24h setting).
// The mirror of footer.h. Passive Layer that draws its model and refreshes the
// clock once a minute on its own. No focus, no interaction.
//
// Reserve HEADER_H at the top of the window for it; place the content area below
// it at y = HEADER_H with height (window height - HEADER_H). Unlike the footer
// (one ever live, on the game board), headers stack with menus, so the minute
// tick fans out to every live header — hidden ones redraw cheaply under the top
// window.
// =============================================================================

#define HEADER_H 24

typedef struct Header Header;

// App-level header config, shared by every container that can show a header
// (menu, paged_list). Set once at startup and whenever a setting toggles; reads
// on the next (re)load — live windows don't repaint until then.
void     header_set_enabled(bool enabled);
bool     header_enabled(void);
// Bitmap drawn at the left edge of the bar (a resource id; 0 = none). Branding
// belongs to the app, not this generic lib, so the caller supplies the icon.
void     header_set_icon(uint32_t res);
uint32_t header_icon(void);

// Create a header pinned to the top of `parent` (a window's root layer) and add
// it as a child. `title` is copied. `icon_res` (0 = none) is drawn at the left,
// before the title. The first live header subscribes the minute tick; the last
// to be destroyed unsubscribes.
Header *header_create(Layer *parent, const char *title, uint32_t icon_res);
void    header_set_title(Header *h, const char *title);
void    header_destroy(Header *h);
