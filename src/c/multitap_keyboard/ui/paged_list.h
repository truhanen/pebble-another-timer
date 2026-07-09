#pragma once
#include <pebble.h>
#include "list_item.h"

// =============================================================================
// paged_list — an interactive list with a bottom pagination bar. Built for the
// 4-button-pager-with-focus-ring case Pebble can't express spatially (no
// left/right): the focus ring runs Up/Down through the item rows and then the
// enabled pager buttons (⏮ First · ◀ Prev · Next ▶ · Last ⏭) in sequence — the
// buttons are drawn horizontally but focused one at a time. Select activates the
// focused thing; Back pops.
//
// The host owns "what's on this page": get_count/get_item describe the current
// page's rows, get_pager describes the pager (page index, totals, which arrows
// are live, a busy label). on_nav fires when a pager button is pressed — the
// host loads the page and calls paged_list_reload() (then paged_list_top() to
// jump back to the first row).
// =============================================================================

typedef enum { PAGER_FIRST, PAGER_PREV, PAGER_NEXT, PAGER_LAST } PagerNav;

typedef struct {
  int  page;          // 0-based current page (for the "N / M" indicator)
  int  total_pages;   // >= 1; 0 → unknown (shown as "?")
  bool has_prev;      // enable First + Prev
  bool has_next;      // enable Next + Last
  bool busy;          // show `status`, disable all nav
  char status[24];    // left label (e.g. "Syncing…"); empty → show "page / total"
} PagerModel;

typedef struct PagedList PagedList;

typedef struct {
  void    *ctx;
  UiSize   size;
  uint16_t (*get_count)(void *ctx);                 // rows on the current page (>= 1; use a placeholder when empty)
  void     (*get_item)(void *ctx, uint16_t i, ListItem *out);
  void     (*on_select)(void *ctx, uint16_t i);     // item row activated (may be NULL)
  void     (*get_pager)(void *ctx, PagerModel *out); // pager state (NULL → no pager bar at all)
  void     (*on_nav)(void *ctx, PagerNav nav);       // pager button activated
  void     (*on_unload)(void *ctx);                  // window popped/destroyed (may be NULL)
} PagedListConfig;

PagedList *paged_list_push(const char *title, PagedListConfig cfg);
void       paged_list_reload(PagedList *p);   // re-read rows + pager, redraw (keeps focus)
void       paged_list_top(PagedList *p);      // select the first row, move focus to the list
Window    *paged_list_window(PagedList *p);
