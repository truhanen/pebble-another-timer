#pragma once
#include <pebble.h>

// =============================================================================
// scrollview — vertically-scrolling content that does NOT use Pebble's
// ScrollLayer. The content is drawn by a callback into a tall box and shifted by
// an offset we own; the layer clips it to the viewport. A ScrollLayer brings its
// own button handling, which would compete with the host's click config — so we
// own the offset outright and the host drives input.
//
// Two forms share one engine:
//   * Embeddable — scrollview_create() places the scroll layers inside a region
//     of a window the CALLER owns. The caller's click config calls
//     scrollview_scroll() on Up/Down; when that returns false the content is at
//     that edge, so the caller can do something else (e.g. a pager turns a page).
//     This is how the pager reuses the scroll behavior without nesting windows.
//   * Standalone — scrollview_push() wraps the embeddable form in its own window
//     with the buttons wired up (Up/Down scroll, Select fires on_select, Back
//     pops). Drop-in full-screen scroller.
//
// The scroll affordance comes in two styles:
//   SCROLLVIEW_SCROLLBAR — a thumb on the right edge sized to the content.
//   SCROLLVIEW_CARETS    — the animated up/down caret bubbles (scroll_hint).
// =============================================================================

typedef void    (*ScrollviewDraw)(GContext *ctx, GRect bounds, void *data);
// Optional: content height for a given width (for wrapped text). NULL → content_h.
typedef int16_t (*ScrollviewMeasure)(int width, void *data);

typedef enum {
  SCROLLVIEW_SCROLLBAR,
  SCROLLVIEW_CARETS,
} ScrollviewAffordance;

// Per-held-step distance. The repeat interval is constant, so this is just speed:
// MEDIUM is the default (zero value).
typedef enum {
  SCROLLVIEW_SPEED_MEDIUM,
  SCROLLVIEW_SPEED_SLOW,
  SCROLLVIEW_SPEED_FAST,
} ScrollviewSpeed;

typedef enum { SCROLLVIEW_UP, SCROLLVIEW_DOWN } ScrollviewDir;

// What gets drawn. `measure` (when non-NULL) resolves the content height from the
// width — for wrapped text; otherwise `content_h` is used.
typedef struct {
  ScrollviewDraw    draw;
  ScrollviewMeasure measure;
  int16_t           content_h;
  void             *data;       // passed back to draw / measure
} ScrollviewContent;

typedef struct Scrollview Scrollview;

// --- embeddable form ---------------------------------------------------------

// Create the scroll layers as children of `parent`, filling `frame`. No content
// yet — call scrollview_set_content(). The caller owns `parent`'s window and its
// click config; destroy with scrollview_destroy() on unload.
Scrollview *scrollview_create(Layer *parent, GRect frame,
                              ScrollviewAffordance affordance, ScrollviewSpeed speed);
void        scrollview_destroy(Scrollview *s);

// Swap the drawn content; re-measures and lands at the top (or the bottom, so an
// upward scroll into this content stays continuous).
void scrollview_set_content(Scrollview *s, ScrollviewContent content, bool at_bottom);

// Scroll one step in `dir`. Returns true if it moved; false if already at that
// edge — the caller can then turn a page, leave the screen, etc.
bool scrollview_scroll(Scrollview *s, ScrollviewDir dir);

bool scrollview_at_top(const Scrollview *s);
bool scrollview_at_bottom(const Scrollview *s);

// Reserve `inset` px at the bottom of the viewport: content lays out and scrolls
// within the remaining height (a short page bottom-anchors above the strip; a
// tall page's last line comes to rest above it), while the layer itself still
// spans the full frame so a caller's overlay (e.g. a pager's floating dots) can
// sit over the strip. Default 0.
void scrollview_set_bottom_inset(Scrollview *s, int inset);

void scrollview_set_affordance(Scrollview *s, ScrollviewAffordance a);
void scrollview_set_speed(Scrollview *s, ScrollviewSpeed speed);
ScrollviewAffordance scrollview_affordance(const Scrollview *s);
ScrollviewSpeed      scrollview_speed(const Scrollview *s);

// --- standalone form ---------------------------------------------------------

typedef struct {
  ScrollviewContent    content;
  ScrollviewAffordance affordance;
  ScrollviewSpeed      speed;
  void               (*on_select)(void *data);   // Select handler; NULL → ignored
} ScrollviewOpts;

// Push a full-screen scrolling window; it frees itself when popped. The returned
// handle is valid while the screen is on the stack (e.g. for an options window
// pushed on top that flips affordance/speed live).
Scrollview *scrollview_push(ScrollviewOpts opts);
