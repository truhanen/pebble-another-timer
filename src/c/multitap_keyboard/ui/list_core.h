#pragma once
#include <pebble.h>
#include "list_item.h"

// =============================================================================
// list_core — PRIVATE to the ui lib. The MenuLayer-in-a-window plumbing shared
// by the public `menu` (interactive) and `list` (display-only) containers: it
// owns the window lifecycle, the icon cache, and routes MenuLayer callbacks to
// list_item_draw / list_item_height. It backs menu.h; apps use that, not this.
// =============================================================================

typedef struct ListCore ListCore;

typedef struct {
  void     *ctx;
  UiSize    size;
  bool      interactive;                            // true → focusable, selectable
  uint16_t (*get_count)(void *ctx);
  void     (*get_item)(void *ctx, uint16_t i, ListItem *out);
  void     (*on_select)(void *ctx, uint16_t i);     // NULL when !interactive
  void     (*on_unload)(void *ctx);                 // window popped/destroyed (may be NULL)
  // Optional section map. When get_sections is set, the list draws a titled
  // header above each section; get_item / on_select keep their *flat* row index
  // (counted across sections, headers excluded), so single-section callers are
  // unaffected. get_section_count must sum to get_count.
  uint16_t    (*get_sections)(void *ctx);
  uint16_t    (*get_section_count)(void *ctx, uint16_t section);
  const char *(*section_title)(void *ctx, uint16_t section);   // may be NULL/empty
} ListCoreConfig;

// Creates a titled window+menu and pushes it. Reloads on reappear (so changes
// made in sub-windows show on return). Frees itself when popped.
ListCore *list_core_push(const char *title, ListCoreConfig cfg);
void      list_core_reload(ListCore *c);
Window   *list_core_window(ListCore *c);

// When enabled (and a list has a non-empty title), a top header bar shows the
// title and a live clock; the menu shrinks to fit below it. App-level setting:
// set once at startup and on toggle. Takes effect on the next load or reload.
void      list_core_set_header_enabled(bool enabled);
bool      list_core_header_enabled(void);
