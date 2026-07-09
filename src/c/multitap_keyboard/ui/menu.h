#pragma once
#include <pebble.h>
#include "list_item.h"

// =============================================================================
// menu — an interactive list in its own window. Up/Down move, Select activates,
// Back pops. Each row is a ListItem (title + optional subtitle, optional leading
// icon, optional trailing accessory). This is the app's navigation primitive.
//
// For display-only, heterogeneous content (sections, fields, gaps) use view.h.
// =============================================================================

typedef struct ListCore Menu;

typedef struct {
  void     *ctx;
  UiSize    size;                                   // row size (default MD)
  uint16_t (*get_count)(void *ctx);
  // Fill `out` (already zeroed to an empty title-only row) for row i.
  void     (*get_item)(void *ctx, uint16_t i, ListItem *out);
  void     (*on_select)(void *ctx, uint16_t i);     // may be NULL
  void     (*on_unload)(void *ctx);                 // window popped/destroyed; may be NULL
  // Optional sections: set get_sections to draw a titled header above each
  // section. get_item / on_select still take a *flat* row index (across all
  // sections, headers excluded), so get_section_count must sum to get_count.
  uint16_t    (*get_sections)(void *ctx);
  uint16_t    (*get_section_count)(void *ctx, uint16_t section);
  const char *(*section_title)(void *ctx, uint16_t section);   // may be NULL/empty
} MenuConfig;

Menu   *menu_push(const char *title, MenuConfig cfg);
void    menu_reload(Menu *m);
Window *menu_window(Menu *m);                        // for window_stack_remove

// Global: show a top header bar (page title + live clock) on titled menus.
// Set at startup from the user's setting and whenever it's toggled; applies on
// the next push or menu_reload.
void    menu_set_header_enabled(bool enabled);
bool    menu_header_enabled(void);
// Global: a brand icon shown at the left of the header bar (a bitmap resource
// id; 0 = none). Set once at startup. Shared with every header-bearing list.
void    menu_set_header_icon(uint32_t res);
