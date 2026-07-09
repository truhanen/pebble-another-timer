#pragma once
#include <pebble.h>
#include "ui_theme.h"

// Stateless row model and drawer shared by menus and static lists.
// Layout: [leading] title / subtitle [trailing]

// Row geometry. Controls cell height and the title/subtitle fonts. MD is the
// default (zero value); LG is the old, taller default; SM is a compact list.
typedef enum { UI_SIZE_MD, UI_SIZE_SM, UI_SIZE_LG } UiSize;

// Interaction state used to resolve row colors.
typedef struct {
  bool highlighted;   // the container's *animated* focus (menu_cell_layer_is_highlighted)
  bool interactive;   // false → display-only row; `highlighted` is ignored
  bool disabled;      // muted ink; a soft fill when focused; inert
  bool danger;        // destructive action: danger-red ink, a red bar when focused
} RowState;

typedef struct {
  GColor fg;          // text + tinted-icon ink
  GColor fill;        // background to paint over the cell (only when `paint`)
  bool   paint;       // override the cell background (the disabled-focus case)
} RowColors;

// Resolve a RowState to concrete colors per the active ui_theme.
RowColors list_item_colors(RowState state);

// Optional leading/trailing accessories.
// ACC_CUSTOM is for call-site-owned glyphs such as crowns.
// Slot support: ICON / ICON_RAW / VALUE / CUSTOM draw in either slot;
// CHECKBOX / CHECK / CHEVRON are trailing-only (silently ignored when leading).
typedef enum {
  ACC_NONE,
  ACC_ICON,       // ~25px resource id, recolored to the row's ink
  ACC_ICON_RAW,   // ~25px resource id, drawn as-authored (two-tone glyphs); not tinted
  ACC_CHECKBOX,   // outlined box, tick when `checked` (trailing only)
  ACC_CHECK,      // bare tick, no box — a selected-item marker (trailing only)
  ACC_CHEVRON,    // ">" disclosure (trailing only)
  ACC_VALUE,      // short text (e.g. "On", "12", "3.")
  ACC_CUSTOM,     // the app draws it
} AccessoryKind;

typedef void (*AccessoryDraw)(GContext *ctx, GRect box, RowColors colors, void *data);

typedef struct {
  AccessoryKind kind;
  union {
    uint32_t icon_res;                 // ACC_ICON / ACC_ICON_RAW
    bool     checked;                  // ACC_CHECKBOX
    char     value[12];                // ACC_VALUE
    struct {                           // ACC_CUSTOM
      AccessoryDraw draw;
      void         *data;
    } custom;
  };
} Accessory;

// --- the row descriptor -------------------------------------------------------
typedef struct {
  char      title[32];
  char      subtitle[32];   // empty → a shorter title-only row
  Accessory leading;
  Accessory trailing;
  bool      disabled;
  bool      danger;         // destructive action: render in the danger palette
  int8_t    content_dy;     // vertical nudge (px) for the whole row's content
} ListItem;

// A clean title-only row. Call before filling fields in a get_item callback.
static inline ListItem list_item_empty(void) { return (ListItem){0}; }

// Container-owned icon lookup/cache hook for ACC_ICON / ACC_ICON_RAW rows.
//
// ACC_ICON bitmaps are tinted IN PLACE (palette recolor) while ACC_ICON_RAW
// draws as authored — the two must never share a bitmap, or the tint bleeds
// into the raw rendering. A raw lookup therefore carries LIST_ICON_RAW_BIT in
// `res`: cache on the full key, strip the bit before loading the resource.
#define LIST_ICON_RAW_BIT 0x80000000u
typedef GBitmap *(*ListIconResolver)(void *ctx, uint32_t res);

// Cell height for `item` at `size` (taller when it has a subtitle).
int16_t list_item_height(const ListItem *item, UiSize size);

// Draw `item` into `bounds`. `resolve`/`resolve_ctx` supply icon bitmaps for
// ACC_ICON slots and may be NULL when a caller has no icons.
void list_item_draw(GContext *ctx, GRect bounds, const ListItem *item,
                    RowState state, UiSize size,
                    ListIconResolver resolve, void *resolve_ctx);
