#pragma once
#include <pebble.h>
#include "list_item.h"
#include "ui_text.h"
#include "footer.h"

// =============================================================================
// view — a scrollable screen that stacks composable blocks. Use it for static
// display content (a results screen is a "Results" section of standings rows, a
// gap, then a "Stats" section of fields) and, with the optional bits below, for
// dynamic screens that own a pinned footer action (the game board's undo).
//
// Up/Down scroll (at the configured speed); a visual scrollbar shows whenever
// the content overflows. Back pops, Select fires on_select.
//
//   static:   build a local Block[] and view_push() it (the view copies it).
//   dynamic:  set ViewOpts.build — the view (re)builds its blocks from that
//             callback on appear and on view_reload(), ignoring the passed array.
// =============================================================================

// Vertical spacing scale (a raw pixel count is also accepted by block_gap).
typedef enum {
  GAP_XS = 2, GAP_SM = 4, GAP_MD = 8, GAP_LG = 12, GAP_XL = 16,
} GapSize;

// Up/Down scroll step. MED is the default (zero value).
typedef enum { SCROLL_MED, SCROLL_SLOW, SCROLL_FAST } ScrollSpeed;

typedef enum { BLOCK_SECTION, BLOCK_ITEM, BLOCK_FIELD, BLOCK_GAP, BLOCK_CUSTOM } BlockKind;

// A custom block draws itself — the escape hatch for app-specific rows (a live
// scoreboard line with its own highlight, badges and miss dots) that don't fit
// the ListItem slots, mirroring ACC_CUSTOM for accessories.
typedef void (*BlockDraw)(GContext *ctx, GRect bounds, void *data);

typedef struct {
  BlockKind kind;
  union {
    char     section[32];                 // BLOCK_SECTION: a titled divider
    ListItem item;                        // BLOCK_ITEM: a list_item row
    struct {                              // BLOCK_FIELD: a label + value
      char label[32], value[32];
      bool stacked;                       // true = value under label; false = inline
    } field;
    int16_t  gap;                         // BLOCK_GAP: height in pixels
    struct {                              // BLOCK_CUSTOM: app-drawn row
      int16_t   h;
      BlockDraw draw;
      void     *data;
    } custom;
  };
} Block;

// Block constructors. Build a local array, e.g.
//   Block b[] = { block_section("Stats"), block_field("Avg per turn", "9.1 pts") };
Block block_section(const char *title);
Block block_item(ListItem item);
Block block_field(const char *label, const char *value);          // stacked
Block block_field_inline(const char *label, const char *value);   // label left / value right
Block block_gap(int16_t pixels);                                  // GAP_* token or a raw count
Block block_custom(int16_t height, BlockDraw draw, void *data);   // app-drawn row

typedef struct {
  UiSize       size;                      // size for ITEM rows (default MD)
  ScrollSpeed  speed;                     // Up/Down scroll step (default medium)
  bool         hide_scrollbar;            // the scrollbar shows by default
  void       (*on_select)(void);          // Select acts on the whole screen

  // Optional: when set, Back calls this instead of popping the window — an escape
  // hatch for a screen-level Back action (e.g. opening an in-game menu). The
  // handler owns what happens next (pop, push, remove this window, …).
  void       (*on_back)(void);

  // Dynamic content: when set, the view (re)builds its blocks by calling this on
  // appear and on view_reload(), ignoring any pushed array. Write up to `cap`
  // blocks into `out` and return the count. `avail_h` is the scroll viewport
  // height (window minus footer), so rows can be sized to fit.
  int        (*build)(Block *out, int cap, int avail_h);

  // Optional pinned footer with a focusable action (e.g. undo). The content area
  // shrinks by FOOTER_H. Pressing Down past the bottom of the scroll arms the
  // action (when can_arm() is true); Up disarms. While armed, Select calls
  // on_action instead of on_select. get_footer fills the left label / clock /
  // icon each refresh; the view supplies the action's enabled + focused state.
  bool         footer;
  void       (*get_footer)(FooterModel *out);
  bool       (*can_arm)(void);
  void       (*on_action)(void);
} ViewOpts;

typedef struct View View;

// Copies `blocks` (ignored when opts.build is set) and pushes the screen. Frees
// itself when popped. Returns the handle for view_reload / view_window.
View   *view_push(const Block *blocks, int n, ViewOpts opts);
void    view_reload(View *v);             // rebuild (opts.build), refresh footer, redraw
Window *view_window(View *v);             // for window_stack_remove
bool    view_action_focused(View *v);     // is the footer action armed? (for custom rows that react)
