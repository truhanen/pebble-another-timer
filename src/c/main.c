// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen
#include <pebble.h>
#include "timer_calc.h"
#include "timer_store.h"
#include "dial_touch.h"
#include "multitap_keyboard/multitap_keyboard/multitap_keyboard_window.h"
#include <string.h>

static Window *s_window;
static MenuLayer *s_menu;
static Layer    *s_empty_hint_layer;
static Layer    *s_main_bottom_bar_layer;
static int16_t   s_menu_selected_timer_idx = -1; // logical timer index selected in list (-1 => + New timer)
static bool      s_menu_internal_selection = false; // guards re-entrant selection callback during reselect
static int16_t   s_menu_visual_y = 0, s_menu_visual_h = 0;
static bool      s_menu_visual_valid = false;
static AppTimer *s_tick;

// Full-screen alarm shown when a timer reaches zero.
static Window *s_alarm_window;
static TextLayer *s_alarm_title;
static TextLayer *s_alarm_sub;
static TextLayer *s_alarm_lbl_up;    // "+1 Min" next to the UP button
static TextLayer *s_alarm_lbl_down;  // "Stop"  next to the DOWN button
static int s_alarm_idx = -1;                 // config index the alarm screen is for
static char s_alarm_title_buf[NAME_LEN + 1]; // big name (or time if unnamed)
static char s_alarm_sub_buf[48];

// Repeating "alarm clock" buzz: re-fire alarm_vibrate() on a timer until the
// user dismisses, capped at ALARM_BUZZ_MAX_S so an unattended watch stops
// buzzing (stock Pebble alarm caps at 10 min; see PebbleOS alarm_popup.c).
#define ALARM_BUZZ_INTERVAL_MS 4000   // ~pattern length (2.8s) + a short gap
#define ALARM_BUZZ_MAX_S       600    // 10 min, then stop buzzing (screen stays)
static AppTimer *s_alarm_buzz_timer;
static int64_t   s_alarm_buzz_start_s;

static Timer s_timers[MAX_TIMERS];
static bool s_delete_on_finish[MAX_TIMERS];  // true => timer is deleted (not kept) once it finishes/stops; never phone-synced while true
static int8_t s_unnamed_star[MAX_TIMERS]; // unnamed timers: per-duration creation-order rank for stable sorting
static int s_count = 0;
static int s_order[MAX_TIMERS];   // display order, rebuilt on reload per s_sort
static SortMode s_sort = SORT_MRU;
static int s_last_fired_idx = -1; // first timer that newly expired in the latest sweep
static bool s_auto_return = false; // config: pop to watchface after a start/resume
static bool s_running_first = true; // config: group RUNNING first, then PAUSED
static bool s_default_finish_delete = true; // config: "Default action after timer finishes" default for new timers
static bool s_launch_sync = false; // config: subtract elapsed-from-launch on starts
static bool s_run_on_create = true; // config: start a newly created timer immediately
static int64_t s_app_launch_s = 0; // app launch timestamp for launch-sync elapsed

// ---- per-timer detail window: long-press menu workflow ----
static Window *s_detail_window;
static MenuLayer *s_detail_menu;
static Layer *s_detail_bottom_bar_layer;
static int16_t s_detail_idx = -1;   // config index the detail window is showing
static DetailAction s_detail_acts[7];
static int8_t s_detail_act_count = 0;
static int16_t s_new_timer_idx = -1;  // index in s_timers of an un-started draft new timer, or -1
static int32_t s_detail_edit_secs = 60;
// After-finished state ("Save", i.e. delete_on_finish == false) as of when the
// LONG_EXISTING detail menu was opened for s_detail_idx -- used to only confirm
// on exit if it was actually changed Save -> Delete during this menu visit.
static bool s_detail_del_on_finish_was_save;
typedef enum { DSTYLE_LEGACY = 0, DSTYLE_LONG_EXISTING = 1, DSTYLE_LONG_NEW = 2 } DetailStyle;
static DetailStyle s_detail_style = DSTYLE_LEGACY;
static int16_t s_label_target_idx = -1; // timer index receiving keyboard-entered label
static DetailStyle s_label_return_style = DSTYLE_LONG_NEW; // detail style to restore after label input
static bool s_detail_advancing = false; // true while pushing a modal from detail window
static bool s_new_flow_open_label_after_dial = false; // chain new timer: dial -> label -> menu
static AppTimer *s_new_flow_label_timer = NULL;
static AppTimer *s_dial_touch_confirm_timer = NULL;
static int16_t s_new_flow_label_idx = -1;
static bool s_dial_existing_duration_edit = false; // true while editing existing timer duration from edit menu

// ---- time edit dial window (opened before long-press/new confirmation menu) ----
static Window *s_dial_window;
static Layer *s_dial_layer;
static int8_t s_dial_field = 1; // 0=hour, 1=minute, 2=second
static bool s_dial_advancing = false; // true while transitioning dial -> confirmation
static AppTimer *s_dial_hold_timer;
static int8_t s_dial_hold_delta; // +1 (UP) or -1 (DOWN)
static uint16_t s_dial_hold_steps;
static uint32_t s_dial_hold_next_due_ms;
#define DIAL_HMS_FIRST_REPEAT_MS 200
#define DIAL_HOLD_POLL_MS 20
#define DIAL_TRI_HW 10
#define DIAL_TRI_H 12

// ---- transient "Started" confirmation shown ~1.1s before auto-return closes the app ----
static Window  *s_confirm_window;
static Layer   *s_confirm_layer;
static AppTimer *s_confirm_timer;
static char s_confirm_name[NAME_LEN + 1];
static char s_confirm_time[16];
static bool s_confirm_named;

// ---- delete-confirm window: "Delete? <name>" + Select=delete / Back=cancel ----
static Window  *s_del_window;
static Layer   *s_del_layer;
static char     s_del_name[NAME_LEN + 1];
static bool     s_del_confirm_stay_on_detail; // true: cancel just closes the confirm, detail menu stays open
// DELCONF_ACTION: a direct delete action (legacy row / dial long-press) -> Select
// deletes now. DELCONF_EXIT_STOPPED: leaving the menu with delete-on-finish set on
// an already-stopped timer -> Select deletes now (no future finish/stop transition
// left to apply it). DELCONF_EXIT_RUNNING: leaving the menu with delete-on-finish
// set on a still-running timer -> Select just leaves (deletion is deferred to that
// timer's natural finish/stop).
typedef enum { DELCONF_ACTION, DELCONF_EXIT_STOPPED, DELCONF_EXIT_RUNNING } DelConfirmKind;
static DelConfirmKind s_del_confirm_kind;

static int64_t now_s(void) { return (int64_t)time(NULL); }

// Seconds since app launch, regardless of the launch-sync config toggle — used
// for the bottom bar's always-on elapsed display.
static int32_t raw_launch_elapsed_s(void) {
  if (s_app_launch_s <= 0) { return 0; }
  int64_t d = now_s() - s_app_launch_s;
  if (d < 0) { d = 0; }
  if (d > INT32_MAX) { d = INT32_MAX; }
  return (int32_t)d;
}

static int32_t launch_elapsed_s(void) {
  if (!s_launch_sync) { return 0; }
  return raw_launch_elapsed_s();
}

static int32_t launch_adjust_start_secs(int32_t base) {
  int32_t adjusted = base - launch_elapsed_s();
  if (adjusted < 1) { adjusted = 1; }
  return adjusted;
}

static bool launch_sync_applies_for_timer(const Timer *t) {
  if (!s_launch_sync || s_app_launch_s <= 0) { return false; }
  if (!t) { return true; }
  if (t->state == TS_PAUSED) { return false; }
  return true;
}

static int32_t launch_adjust_start_secs_for_timer(const Timer *t, int32_t base) {
  if (!launch_sync_applies_for_timer(t)) {
    if (base < 1) { base = 1; }
    return base;
  }
  return launch_adjust_start_secs(base);
}

#define BOTTOM_BAR_H 28

static int16_t bottom_bar_top_for_bounds(GRect bounds) {
  return bounds.size.h - BOTTOM_BAR_H + 3;
}

static GRect bottom_bar_rect_for_bounds(GRect bounds) {
  return GRect(0, bottom_bar_top_for_bounds(bounds), bounds.size.w, BOTTOM_BAR_H);
}

static GFont bottom_bar_font_for_width(int16_t width) {
  return fonts_get_system_font((width <= 144) ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_24_BOLD);
}

static int16_t bottom_bar_text_h_for_width(int16_t width) {
  return (width <= 144) ? 22 : 28;
}

static bool detail_style_has_bottom_bar(void) {
  return true;
}

static void draw_bottom_bar(GContext *gctx, GRect bounds) {
  graphics_context_set_stroke_color(gctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_context_set_stroke_width(gctx, 1);
  graphics_draw_line(gctx,
    GPoint(bounds.origin.x, bounds.origin.y),
    GPoint(bounds.origin.x + bounds.size.w - 1, bounds.origin.y));
  graphics_context_set_fill_color(gctx, GColorBlack);
  graphics_fill_rect(gctx, GRect(bounds.origin.x, bounds.origin.y + 1, bounds.size.w, bounds.size.h - 1),
    0, GCornerNone);

  char left[16];
  clock_copy_time_string(left, sizeof(left));
  char elapsed[16];
  tc_format_remaining(elapsed, sizeof(elapsed), raw_launch_elapsed_s());
  char right[24];
  snprintf(right, sizeof(right), "%s", elapsed);

  const GFont f = bottom_bar_font_for_width(bounds.size.w);
  const int th = bottom_bar_text_h_for_width(bounds.size.w);
  const int ty = bounds.origin.y + (bounds.size.h - th) / 2 - 4;
  graphics_context_set_text_color(gctx, GColorWhite);
  graphics_draw_text(gctx, left, f, GRect(bounds.origin.x + 4, ty, bounds.size.w - 8, th),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(gctx, right, f, GRect(bounds.origin.x + 4, ty, bounds.size.w - 8, th),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

static void format_launch_sync_suffix(char *buf, size_t n) {
  int32_t e = launch_elapsed_s();
  int m = e / 60;
  int s = e % 60;
  snprintf(buf, n, "-%d:%02d", m, s);
}

// Close the app to the WATCHFACE (not the launcher). exit_reason_set tells
// PebbleOS this was a completed action, so it returns to the watchface; without
// it window_stack_pop_all lands back wherever the app was launched from.
static void close_to_watchface(void) {
  exit_reason_set(APP_EXIT_ACTION_PERFORMED_SUCCESSFULLY);
  window_stack_pop_all(true);
}

// ---- idle auto-exit: return to the watchface after s_idle_timeout_sec of no
// button press in the list/detail views. Armed in each target window's .appear,
// cancelled in .disappear (so the alarm / confirm / delete modals pause it for
// free), and reset by every action handler. 0 = feature off. ----
static int       s_idle_timeout_sec; // seconds; 0 disables
static AppTimer *s_idle_timer;
static bool      s_config_open = false; // true while the phone config page is open (pauses idle)

static void idle_cancel(void) {
  if (s_idle_timer) { app_timer_cancel(s_idle_timer); s_idle_timer = NULL; }
}
static void idle_fire(void *ctx) {
  s_idle_timer = NULL;
  Window *top = window_stack_get_top_window();
  if ((top == s_dial_window || top == s_window) && dial_touch_in_progress()) {
    if (!s_config_open && s_idle_timeout_sec > 0) {
      s_idle_timer = app_timer_register(s_idle_timeout_sec * 1000, idle_fire, NULL);
    }
    return;
  }
  close_to_watchface();
}
static void idle_reset(void) {
  if (s_config_open) return;           // never (re)arm while the phone config page is open
  if (s_idle_timeout_sec <= 0) { idle_cancel(); return; }
  if (s_idle_timer) { app_timer_reschedule(s_idle_timer, s_idle_timeout_sec * 1000); }
  else { s_idle_timer = app_timer_register(s_idle_timeout_sec * 1000, idle_fire, NULL); }
}
// Read the timeout seconds tolerantly: Clay's default auto-send delivers a
// `select` value as a CString; a custom handler sends an int. Returns -1 when
// the key is absent (leave the current value unchanged). NB: hand-rolled digit
// parse — atoi/strtol are NOT exported by the Core firmware (hard fault).
static int idle_read_seconds(Tuple *t) {
  if (!t) { return -1; }
  if (t->type == TUPLE_CSTRING) {
    int v = 0; const char *p = t->value->cstring;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); }
    return v;
  }
  return (int)t->value->int32;
}
static void idle_appear(Window *w) { idle_reset(); }
static void idle_disappear(Window *w) { idle_cancel(); }

// ---- wakeup: keep exactly ONE armed for the soonest running end_time ----
// Arm the NEW wakeup BEFORE giving up the old one, so a transiently-refused
// schedule (slot taken / E_RANGE) can never leave the app with no wakeup at all.
// (The previous order cancelled first, so if every schedule attempt failed the
// app was left wakeup-less and the timer then expired silently while closed — no
// buzz, just a red 00:00:00 on the next open.)
static void rearm_wakeup(void) {
  int32_t old = store_load_wakeup_id();
  int64_t soon;
  if (!tc_soonest_end(s_timers, s_count, &soon)) {
    // No running timers: drop any armed wakeup.
    if (old >= 0) { wakeup_cancel(old); store_save_wakeup_id(-1); }
    return;
  }
  time_t nowt = time(NULL);
  time_t base = (time_t)soon;
  if (base <= nowt) { base = nowt + 1; }

  // 1) Try the exact desired time while KEEPING the old wakeup, so a success never
  //    leaves a gap with nothing armed. Fresh arms and changed-soonest re-arms take
  //    this path and land exactly on time.
  WakeupId id = wakeup_schedule(base, 0, true);
  if (id >= 0) {
    if (old >= 0 && old != id) { wakeup_cancel(old); }
    store_save_wakeup_id(id);
    return;
  }
  // 2) Exact slot refused. The usual reason is our OWN existing wakeup sitting
  //    within the 1-min guard (a redundant re-arm for an unchanged soonest), so
  //    drop the old one and retry the exact time first, then nudge forward a few
  //    minutes for a slot genuinely contended by another app's wakeup.
  if (old >= 0) { wakeup_cancel(old); store_save_wakeup_id(-1); }
  time_t when = base;
  for (int attempt = 0; attempt < 5; attempt++) {
    id = wakeup_schedule(when, 0, true);
    if (id >= 0) { store_save_wakeup_id(id); return; }
    when += 60;   // E_RANGE / slot taken: try the next minute
  }
  APP_LOG(APP_LOG_LEVEL_WARNING, "wakeup_schedule failed after retries");
}

static void persist_all(void) {
  uint32_t mask = 0;
  for (int i = 0; i < s_count && i < 32; i++) {
    if (s_delete_on_finish[i]) { mask |= (1u << i); }
  }
  store_save(s_timers, s_count);
  store_save_delete_on_finish_mask(mask);
}

// Forward declarations used by reload_ui (implemented later in file).
static int ml_row_for_timer_primary(int timer_idx, int selected_idx);
static int ml_row_for_new(int selected_idx);
static bool ml_block_for_selection(int selected_idx, int *out_y, int *out_h);
static void ml_scroll_item_bounds_into_view(int item_y, int item_h);

static void rebuild_order(void) {
  tc_display_order(s_timers, s_count, s_sort, now_s(), s_order, s_running_first);
  // For unnamed timers with identical durations, keep creation order (fewer '*' first).
  for (int i = 1; i < s_count; i++) {
    int key = s_order[i];
    if (s_timers[key].name[0] != 0) { continue; }
    int key_d = s_timers[key].duration;
    int key_s = s_unnamed_star[key];
    if (key_s < 0) { continue; }
    int j = i - 1;
    while (j >= 0) {
      int cur = s_order[j];
      if (s_timers[cur].name[0] != 0) { break; }
      if (s_timers[cur].duration != key_d) { break; }
      int cur_s = s_unnamed_star[cur];
      if (cur_s < 0 || cur_s <= key_s) { break; }
      s_order[j + 1] = cur;
      j--;
    }
    s_order[j + 1] = key;
  }
}

static void reload_ui(void) {
  rebuild_order();
  if (s_menu_selected_timer_idx != -1) {
    int row = ml_row_for_timer_primary(s_menu_selected_timer_idx, s_menu_selected_timer_idx);
    if (row < 0) { s_menu_selected_timer_idx = (s_count > 0) ? (int16_t)s_order[0] : -1; }
  }
  if (s_menu) {
    menu_layer_reload_data(s_menu);
    int target_row = (s_menu_selected_timer_idx == -1)
      ? ml_row_for_new(s_menu_selected_timer_idx)
      : ml_row_for_timer_primary(s_menu_selected_timer_idx, s_menu_selected_timer_idx);
    if (target_row >= 0) {
      s_menu_internal_selection = true;
      menu_layer_set_selected_index(s_menu, (MenuIndex){ .section = 0, .row = (uint16_t)target_row }, MenuRowAlignNone, false);
      s_menu_internal_selection = false;
      int y = 0, h = 0;
      if (ml_block_for_selection(s_menu_selected_timer_idx, &y, &h)) {
        s_menu_visual_y = (int16_t)y;
        s_menu_visual_h = (int16_t)h;
        s_menu_visual_valid = true;
        ml_scroll_item_bounds_into_view(y, h);
      }
    }
  }
  if (s_empty_hint_layer) { layer_mark_dirty(s_empty_hint_layer); }
}

static int next_unnamed_star(int32_t duration) {
  int m = -1;
  for (int i = 0; i < s_count; i++) {
    if (s_timers[i].name[0] != 0) { continue; }
    if (s_timers[i].duration != duration) { continue; }
    if (s_unnamed_star[i] > m) { m = s_unnamed_star[i]; }
  }
  return m + 1;
}

static int next_unnamed_star_excluding(int32_t duration, int exclude_idx) {
  int m = -1;
  for (int i = 0; i < s_count; i++) {
    if (i == exclude_idx) { continue; }
    if (s_timers[i].name[0] != 0) { continue; }
    if (s_timers[i].duration != duration) { continue; }
    if (s_unnamed_star[i] > m) { m = s_unnamed_star[i]; }
  }
  return m + 1;
}

static void ensure_unnamed_star(int idx) {
  if (idx < 0 || idx >= s_count) { return; }
  Timer *t = &s_timers[idx];
  if (t->name[0] != 0) { return; }
  if (s_unnamed_star[idx] >= 0) { return; }
  s_unnamed_star[idx] = next_unnamed_star(t->duration);
}

static void assign_unnamed_star_for_duration(int idx, int32_t duration) {
  if (idx < 0 || idx >= s_count) { return; }
  if (s_timers[idx].name[0] != 0) { s_unnamed_star[idx] = -1; return; }
  s_unnamed_star[idx] = next_unnamed_star_excluding(duration, idx);
}

static void ensure_ticking(void);   // defined below; used by start/alarm handlers
static void open_detail_window(int timer_idx, DetailStyle style);  // defined below; used by alarm + menus
static void open_dial_window(int timer_idx, DetailStyle style);    // defined below; used by long/new flows
static void dial_touch_selected(uint8_t hours, uint8_t minutes, uint8_t seconds);
static void main_touch_selected(uint8_t hours, uint8_t minutes, uint8_t seconds);
static void open_delete_confirm(bool stay_on_detail_on_cancel, DelConfirmKind kind); // defined below; delete confirm modal
static void remove_timer_at(int idx); // defined below; used by alarm stop/delete paths
static int sweep_expiries(void); // defined below; used by tick/start helpers
static void trigger_alarm(int idx, int count); // defined below; alarm UI path
static int ml_row_for_timer_primary(int timer_idx, int selected_idx); // defined below; list row mapping
static int ml_row_for_new(int selected_idx); // defined below; list row mapping for trailing + New timer row
static bool ml_block_for_selection(int selected_idx, int *out_y, int *out_h); // defined below; highlight block
static bool ml_current_highlight_rect(int *out_y, int *out_h); // defined below; animated/static rect
static void ml_scroll_item_bounds_into_view(int item_y, int item_h); // defined below
static void apply_overwrite_only(int idx, int32_t secs, const char *name); // defined below

static void start_with_secs(Timer *t, int32_t secs) {
  tc_extend(t, secs, now_s());  // secs may be <=0 (immediate expiry on next sweep/check)
}

static bool finish_start_tail(void) {
  int fired = sweep_expiries();
  persist_all(); rearm_wakeup(); ensure_ticking(); reload_ui();
  if (fired) { trigger_alarm(s_last_fired_idx, fired); return true; }
  return false;
}

// Mark every expired RUNNING timer DONE. Returns the count that NEWLY expired and
// sets s_last_fired_idx to the first of them (drives the alarm screen). No UI here.
static int sweep_expiries(void) {
  int fired = 0;
  int64_t now = now_s();
  for (int i = 0; i < s_count; i++) {
    if (tc_check_expiry(&s_timers[i], now)) {
      if (fired == 0) { s_last_fired_idx = i; }
      fired++;
    }
  }
  return fired;
}

// ---- full-screen alarm when a timer finishes ----
static void alarm_vibrate(void) {
  // a bit longer than a single long pulse: several long buzzes
  static const uint32_t segs[] = {500, 200, 500, 200, 500, 200, 700};
  VibePattern pat = { .durations = segs, .num_segments = ARRAY_LENGTH(segs) };
  vibes_enqueue_custom_pattern(pat);
}

// Re-buzz on a repeating timer until the 10-min cap; then stop scheduling but
// leave the alarm window up (the user must press Stop/+1 Min to dismiss).
static void alarm_buzz_cb(void *ctx) {
  s_alarm_buzz_timer = NULL;
  if (now_s() - s_alarm_buzz_start_s >= ALARM_BUZZ_MAX_S) { return; }
  alarm_vibrate();
  s_alarm_buzz_timer = app_timer_register(ALARM_BUZZ_INTERVAL_MS, alarm_buzz_cb, NULL);
}

static void alarm_buzz_start(void) {
  if (s_alarm_buzz_timer) { app_timer_cancel(s_alarm_buzz_timer); s_alarm_buzz_timer = NULL; }
  s_alarm_buzz_start_s = now_s();
  alarm_vibrate();   // buzz now
  s_alarm_buzz_timer = app_timer_register(ALARM_BUZZ_INTERVAL_MS, alarm_buzz_cb, NULL);
}

static void alarm_buzz_stop(void) {
  if (s_alarm_buzz_timer) { app_timer_cancel(s_alarm_buzz_timer); s_alarm_buzz_timer = NULL; }
  vibes_cancel();
}

static void alarm_stop(ClickRecognizerRef rec, void *ctx) {
  // Stop: reset the finished timer directly from the alarm, then close the app
  // (-> watchface). The alarm often fires while the app is closed (wakeup-launched),
  // so after dismissing the user wants the watchface, not to be left in the app.
  if (s_alarm_idx >= 0 && s_alarm_idx < s_count) {
    if (s_delete_on_finish[s_alarm_idx]) { remove_timer_at(s_alarm_idx); }
    else { tc_reset(&s_timers[s_alarm_idx], now_s()); }
    persist_all(); rearm_wakeup(); reload_ui();
  }
  close_to_watchface();
}

static void alarm_add_minute(ClickRecognizerRef rec, void *ctx) {
  // Snooze: run the finished timer for 1 more minute, dismiss the alarm, and land on
  // that timer's own detail window. Push the detail window over the alarm first, then
  // silently drop the alarm from beneath it (no flash back to the list).
  int idx = s_alarm_idx;
  if (idx >= 0 && idx < s_count) {
    tc_extend(&s_timers[idx], 60, now_s());
    persist_all(); rearm_wakeup(); ensure_ticking(); reload_ui();
    open_detail_window(idx, DSTYLE_LEGACY);
    window_stack_remove(s_alarm_window, false);
  } else {
    window_stack_remove(s_alarm_window, true);
  }
}

static void alarm_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_DOWN, alarm_stop);        // Stop: reset + dismiss
  window_single_click_subscribe(BUTTON_ID_UP, alarm_add_minute);    // +1 Min: snooze + dismiss
  window_single_click_subscribe(BUTTON_ID_BACK, alarm_add_minute);  // Back = snooze (matches stock)
}

// Pick the largest title font whose word-wrapped layout fits within box_h (the
// vertical band between the +1 Min and Stop labels), so a long timer name shrinks
// instead of overflowing the band and getting clipped mid-line. Returns the chosen
// font and writes its measured wrapped size into *out. Falls back to the smallest
// font if even that overflows (still better than the fixed 42px that clipped).
static GFont alarm_title_font(const char *text, int box_w, int box_h, GSize *out) {
  static const char *const keys[] = {
    FONT_KEY_BITHAM_42_BOLD,
    FONT_KEY_BITHAM_30_BLACK,
    FONT_KEY_GOTHIC_28_BOLD,
    FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_18_BOLD,
  };
  const GRect probe = GRect(0, 0, box_w, 2000);
  GFont chosen = NULL;
  for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    GFont f = fonts_get_system_font(keys[i]);
    GSize sz = graphics_text_layout_get_content_size(
        text, f, probe, GTextOverflowModeWordWrap, GTextAlignmentCenter);
    chosen = f; *out = sz;
    if (sz.h <= box_h) { break; }   // largest font that fits vertically -> use it
  }
  return chosen;
}

// (Re)compute the title layer's font + frame from the current name. The available
// band is between the bottom of the +1 Min label (~22% h) and the top of Stop
// (~78% h); the text is vertically centred within it. Called on load and on
// in-place refresh (a second timer finishing reuses the open alarm window).
static void layout_alarm_title(void) {
  if (!s_alarm_title || !s_alarm_window) { return; }
  GRect b = layer_get_bounds(window_get_root_layer(s_alarm_window));
  const int h = b.size.h, wd = b.size.w;
  const int up_bottom = h * 22 / 100 - 16 + 34;   // bottom edge of the +1 Min label
  const int down_top  = h * 78 / 100 - 18;         // top edge of the Stop label
  const int band_top = up_bottom + 2;
  const int band_h   = down_top - band_top - 2;
  const int box_w = wd - 4;
  GSize sz;
  GFont tf = alarm_title_font(s_alarm_title_buf, box_w, band_h, &sz);
  const int used_h = sz.h < band_h ? sz.h : band_h;
  const int title_y = band_top + (band_h - used_h) / 2;
  text_layer_set_font(s_alarm_title, tf);
  layer_set_frame(text_layer_get_layer(s_alarm_title), GRect(2, title_y, box_w, used_h + 4));
}

static void alarm_window_load(Window *w) {
  window_set_background_color(w, GColorRed);
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  const int h = b.size.h, wd = b.size.w;

  // "+N more" — small, top-centre (free space; the UP label is right-aligned).
  s_alarm_sub = text_layer_create(GRect(4, 2, wd - 8, 22));
  text_layer_set_background_color(s_alarm_sub, GColorClear);
  text_layer_set_text_color(s_alarm_sub, GColorWhite);
  text_layer_set_font(s_alarm_sub, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_alarm_sub, GTextAlignmentCenter);
  text_layer_set_text(s_alarm_sub, s_alarm_sub_buf);
  layer_add_child(root, text_layer_get_layer(s_alarm_sub));

  // "+1 Min" — big bold, right-aligned, vertically by the UP button (~22% h).
  s_alarm_lbl_up = text_layer_create(GRect(0, h * 22 / 100 - 16, wd - 6, 34));
  text_layer_set_background_color(s_alarm_lbl_up, GColorClear);
  text_layer_set_text_color(s_alarm_lbl_up, GColorWhite);
  text_layer_set_font(s_alarm_lbl_up, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_alarm_lbl_up, GTextAlignmentRight);
  text_layer_set_text(s_alarm_lbl_up, "+1 Min");
  layer_add_child(root, text_layer_get_layer(s_alarm_lbl_up));

  // Title — large bold, centred in the band between the +1 Min and Stop labels
  // (timer name, or time if unnamed). The font auto-shrinks for long, wrapping
  // names so the text never overflows the band and gets clipped mid-line; the
  // frame + font are computed in layout_alarm_title() (also re-run on refresh).
  s_alarm_title = text_layer_create(GRect(2, h / 2 - 36, wd - 4, 72));
  text_layer_set_background_color(s_alarm_title, GColorClear);
  text_layer_set_text_color(s_alarm_title, GColorWhite);
  text_layer_set_text_alignment(s_alarm_title, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_alarm_title, GTextOverflowModeWordWrap);
  text_layer_set_text(s_alarm_title, s_alarm_title_buf);
  layer_add_child(root, text_layer_get_layer(s_alarm_title));
  layout_alarm_title();

  // "Stop" — big bold, right-aligned, vertically by the DOWN button (~78% h).
  s_alarm_lbl_down = text_layer_create(GRect(0, h * 78 / 100 - 18, wd - 6, 34));
  text_layer_set_background_color(s_alarm_lbl_down, GColorClear);
  text_layer_set_text_color(s_alarm_lbl_down, GColorWhite);
  text_layer_set_font(s_alarm_lbl_down, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_alarm_lbl_down, GTextAlignmentRight);
  text_layer_set_text(s_alarm_lbl_down, "Stop");
  layer_add_child(root, text_layer_get_layer(s_alarm_lbl_down));
}

static void alarm_window_unload(Window *w) {
  alarm_buzz_stop();
  text_layer_destroy(s_alarm_title); s_alarm_title = NULL;
  text_layer_destroy(s_alarm_sub); s_alarm_sub = NULL;
  text_layer_destroy(s_alarm_lbl_up); s_alarm_lbl_up = NULL;
  text_layer_destroy(s_alarm_lbl_down); s_alarm_lbl_down = NULL;
}

// Show the alarm for timer `idx` (first of `count` that just finished): big name,
// long vibration, backlight on briefly; Select resets it.
static void trigger_alarm(int idx, int count) {
  if (idx < 0 || idx >= s_count) { return; }
  s_alarm_idx = idx;
  Timer *t = &s_timers[idx];
  if (t->name[0]) {
    snprintf(s_alarm_title_buf, sizeof(s_alarm_title_buf), "%s", t->name);
  } else {
    tc_format_remaining(s_alarm_title_buf, sizeof(s_alarm_title_buf), t->duration);
  }
  if (count > 1) {
    snprintf(s_alarm_sub_buf, sizeof(s_alarm_sub_buf), "+%d more", count - 1);
  } else {
    s_alarm_sub_buf[0] = '\0';
  }
  light_enable_interaction();   // backlight on for the standard brief window
  alarm_buzz_start();   // repeating buzz until dismissed (cap restarts on each trigger)
  if (!s_alarm_window) {
    s_alarm_window = window_create();
    window_set_window_handlers(s_alarm_window, (WindowHandlers){
      .load = alarm_window_load, .unload = alarm_window_unload });
    window_set_click_config_provider(s_alarm_window, alarm_click_config);
  }
  if (window_stack_get_top_window() == s_alarm_window) {
    // already showing (another timer finished): refresh the text in place
    if (s_alarm_title) { text_layer_set_text(s_alarm_title, s_alarm_title_buf); layout_alarm_title(); }
    if (s_alarm_sub) { text_layer_set_text(s_alarm_sub, s_alarm_sub_buf); }
  } else {
    window_stack_push(s_alarm_window, true);
  }
}

// ---- 1s foreground tick: refresh running rows + catch foreground expiries ----
static void tick_cb(void *ctx) {
  int fired = sweep_expiries();
  bool running = false;
  for (int i = 0; i < s_count; i++) { if (s_timers[i].state == TS_RUNNING) { running = true; } }
  if (running || fired) {
    reload_ui();
  }
  if (s_detail_menu && window_stack_get_top_window() == s_detail_window && s_detail_style == DSTYLE_LEGACY) {
    menu_layer_reload_data(s_detail_menu);   // retick the legacy live-time header
  }
  if (s_dial_layer && window_stack_get_top_window() == s_dial_window) {
    layer_mark_dirty(s_dial_layer);          // retick dial header + bottom bar
  }
  if (s_main_bottom_bar_layer && window_stack_get_top_window() == s_window) {
    layer_mark_dirty(s_main_bottom_bar_layer);
  }
  if (s_detail_bottom_bar_layer && window_stack_get_top_window() == s_detail_window
      && detail_style_has_bottom_bar()) {
    layer_mark_dirty(s_detail_bottom_bar_layer);
  }
  if (fired) { persist_all(); rearm_wakeup(); trigger_alarm(s_last_fired_idx, fired); }
  s_tick = app_timer_register(1000, tick_cb, NULL);
}

static void ensure_ticking(void) {
  if (s_tick) { return; }
  s_tick = app_timer_register(1000, tick_cb, NULL);
}

// ---- per-timer detail window: live-time header + Pause/Stop/+N actions ----

// After acting on a timer it re-sorts (e.g. floats to the top in MRU mode); move the
// LIST cursor to follow it to its new row so the user needn't scroll to it.
static void select_timer_row(int idx) {
  if (!s_menu) { return; }
  s_menu_selected_timer_idx = (int16_t)idx;
  int row = ml_row_for_timer_primary(idx, s_menu_selected_timer_idx);
  if (row < 0) { return; }
  int to_y = 0, to_h = 0;
  if (!ml_block_for_selection(s_menu_selected_timer_idx, &to_y, &to_h)) { return; }
  s_menu_internal_selection = true;
  menu_layer_set_selected_index(s_menu, (MenuIndex){ .section = 0, .row = (uint16_t)row },
                                MenuRowAlignNone, false);
  s_menu_internal_selection = false;
  s_menu_visual_y = (int16_t)to_y;
  s_menu_visual_h = (int16_t)to_h;
  s_menu_visual_valid = true;
  menu_layer_reload_data(s_menu);
  ml_scroll_item_bounds_into_view(to_y, to_h);
}

static int dl_clamp_step(int v, int min, int max, int delta) {
  int x = v + delta;
  if (x < min) { x = min; }
  if (x > max) { x = max; }
  return x;
}

static int dl_wrap_step(int v, int min, int max, int delta) {
  int span = max - min + 1;
  int x = (v - min + delta) % span;
  if (x < 0) { x += span; }
  return min + x;
}

static void dl_secs_to_hms(int32_t secs, int *h, int *m, int *s) {
  if (secs < 0) { secs = 0; }
  if (secs > 363599) { secs = 363599; } // 100:59:59
  *h = secs / 3600;
  *m = (secs % 3600) / 60;
  *s = secs % 60;
}

static void dl_adjust_field(int delta) {
  int h, m, s;
  dl_secs_to_hms(s_detail_edit_secs, &h, &m, &s);
  if (s_dial_field == 0) { h = dl_clamp_step(h, 0, 100, delta); }
  else if (s_dial_field == 1) { m = dl_wrap_step(m, 0, 59, delta); }
  else { s = dl_wrap_step(s, 0, 59, delta); }
  int32_t next = h * 3600 + m * 60 + s;
  s_detail_edit_secs = next;
  if (s_dial_layer) { layer_mark_dirty(s_dial_layer); }
}

static void dial_adjust_current(int delta) {
  dl_adjust_field(delta);
}

static void dial_hold_cancel(void) {
  if (s_dial_hold_timer) {
    app_timer_cancel(s_dial_hold_timer);
    s_dial_hold_timer = NULL;
  }
}

static uint32_t dial_now_ms(void) {
  time_t s_utc = 0;
  uint16_t ms = 0;
  time_ms(&s_utc, &ms);
  return (uint32_t)s_utc * 1000u + (uint32_t)ms;
}

static uint32_t dial_hold_step_interval_ms(int steps_applied) {
  (void)steps_applied;
  return 70;
}

static void dial_hold_repeat_cb(void *ctx) {
  s_dial_hold_timer = NULL;
  uint32_t now_ms = dial_now_ms();
  int guard = 0;
  while ((int32_t)(now_ms - s_dial_hold_next_due_ms) >= 0 && guard < 512) {
    dial_adjust_current(s_dial_hold_delta);
    s_dial_hold_steps++;
    s_dial_hold_next_due_ms += dial_hold_step_interval_ms(s_dial_hold_steps);
    guard++;
  }
  s_dial_hold_timer = app_timer_register(DIAL_HOLD_POLL_MS, dial_hold_repeat_cb, NULL);
}

static void dial_hold_begin_common(int delta) {
  idle_reset();
  s_dial_hold_delta = delta;
  dial_adjust_current(delta); // first step at long-click delay (200ms)
  s_dial_hold_steps = 1;
  dial_hold_cancel();
  s_dial_hold_next_due_ms = dial_now_ms() + DIAL_HMS_FIRST_REPEAT_MS;
  s_dial_hold_timer = app_timer_register(DIAL_HOLD_POLL_MS, dial_hold_repeat_cb, NULL);
}

static void dial_hold_end_common(void) {
  s_dial_hold_steps = 0;
  dial_hold_cancel();
}

static void dl_draw_triangle_up_sized(GContext *gctx, int cx, int y, int hw, int hh, GColor c) {
  graphics_context_set_fill_color(gctx, c);
  graphics_context_set_stroke_color(gctx, c);
  for (int dy = 0; dy <= hh; dy++) {
    int half = (hw * dy) / hh;
    int yy = y + dy;
    graphics_draw_line(gctx, GPoint(cx - half, yy), GPoint(cx + half, yy));
  }
  graphics_draw_line(gctx, GPoint(cx - hw, y + hh), GPoint(cx, y));
  graphics_draw_line(gctx, GPoint(cx, y), GPoint(cx + hw, y + hh));
  graphics_draw_line(gctx, GPoint(cx - hw, y + hh), GPoint(cx + hw, y + hh));
}

static void dl_draw_triangle_down_sized(GContext *gctx, int cx, int y, int hw, int hh, GColor c) {
  graphics_context_set_fill_color(gctx, c);
  graphics_context_set_stroke_color(gctx, c);
  for (int dy = 0; dy <= hh; dy++) {
    int half = hw - (hw * dy) / hh;
    int yy = y + dy;
    graphics_draw_line(gctx, GPoint(cx - half, yy), GPoint(cx + half, yy));
  }
  graphics_draw_line(gctx, GPoint(cx - hw, y), GPoint(cx, y + hh));
  graphics_draw_line(gctx, GPoint(cx, y + hh), GPoint(cx + hw, y));
  graphics_draw_line(gctx, GPoint(cx - hw, y), GPoint(cx + hw, y));
}

static void dial_update_proc(Layer *layer, GContext *gctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(gctx, GColorWhite);
  graphics_fill_rect(gctx, b, 0, GCornerNone);
  graphics_context_set_text_color(gctx, GColorBlack);

  char head_right[36];
  tc_format_remaining(head_right, sizeof(head_right), s_detail_edit_secs);
  GFont hf = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  const char *head_left = NULL;
  if (s_detail_style == DSTYLE_LONG_NEW) {
    if (s_detail_idx >= 0 && s_detail_idx < s_count && s_timers[s_detail_idx].name[0]) {
      head_left = s_timers[s_detail_idx].name;
    } else {
      head_left = "Duration";
    }
  }
  if (s_detail_style == DSTYLE_LONG_EXISTING && s_detail_idx >= 0 && s_detail_idx < s_count) {
    Timer *t = &s_timers[s_detail_idx];
    head_left = t->name[0] ? t->name : "<No label>";
  }
  if (head_left) {
    graphics_draw_text(gctx, head_left, hf, GRect(4, 2, b.size.w - 90, 26),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
  graphics_draw_text(gctx, head_right, hf, GRect(4, 2, b.size.w - 8, 26),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  int margin = 8;
  int gap = 6;
  int avail_w = b.size.w - margin * 2 - gap * 2;
  int bw = avail_w / 3;
  if (bw < 1) { bw = 1; }
  const int dial_extra_h = 20 + 8 + DIAL_TRI_H; // top gap + bottom gap + bottom triangle height
  int bh = (b.size.h * 28) / 100; // current baseline dial-box height
  bh = (bh * 3) / 4;              // reduce by 25%
  int max_bh = b.size.h - dial_extra_h;
  if (max_bh < 1) { max_bh = 1; }
  if (bh > max_bh) { bh = max_bh; }
  if (bh < 1) { bh = 1; }
  int dial_total_h = bh + dial_extra_h;
  int dial_top_y = (b.size.h - dial_total_h) / 2;
  int by = dial_top_y + 20;

  int h, m, s;
  dl_secs_to_hms(s_detail_edit_secs, &h, &m, &s);

  for (int i = 0; i < 3; i++) {
    GColor text_c = GColorBlack;
    int x = margin + i * (bw + gap);

    GRect box = GRect(x, by, bw, bh);
    bool selected = (i == s_dial_field);
    GColor border_c = selected ? GColorBlack : PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack);
    GColor tri_c = selected ? GColorBlack : PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack);
    int cx = x + bw / 2;
    int up_y = by - 20;
    int down_y = by + bh + 8;

    graphics_context_set_stroke_color(gctx, border_c);
    graphics_context_set_stroke_width(gctx, 3);
    graphics_draw_rect(gctx, box);

    char txt[4];
    int v = (i == 0) ? h : ((i == 1) ? m : s);
    if (i == 0) { snprintf(txt, sizeof(txt), "%d", v); }
    else { snprintf(txt, sizeof(txt), "%02d", v); }
    graphics_context_set_text_color(gctx, text_c);
    GFont num_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    int text_h = graphics_text_layout_get_content_size(
      txt, num_font, GRect(0, 0, bw - 4, 200), GTextOverflowModeFill, GTextAlignmentCenter).h;
    // GOTHIC reserves headroom above the caps (see multitap_keyboard.c's font ladder), so a
    // measured content box still sits low when centered; lift it back to the optical middle.
    const int rise = 5;
    const int nudge_x = (i == 0) ? 0 : 1;   // minute/second glyphs shifted 1px right
    graphics_draw_text(gctx, txt, num_font,
      GRect(x + 2 + nudge_x, by + (bh - text_h) / 2 - rise, bw - 4, text_h),
      GTextOverflowModeFill, GTextAlignmentCenter, NULL);

    dl_draw_triangle_up_sized(gctx, cx, up_y, DIAL_TRI_HW, DIAL_TRI_H, tri_c);
    dl_draw_triangle_down_sized(gctx, cx, down_y, DIAL_TRI_HW, DIAL_TRI_H, tri_c);
  }

  const int hint_h = 26;
  bool show_bottom_bar = !dial_touch_in_progress();
  int bottom_reserved = show_bottom_bar ? BOTTOM_BAR_H : 0;
  if (b.size.h >= hint_h) {
    const int hint_y = b.size.h - hint_h - 5 - bottom_reserved;
    graphics_context_set_text_color(gctx, GColorBlack);
    graphics_draw_text(gctx, "Touch opens touch dial",
      fonts_get_system_font(FONT_KEY_GOTHIC_24),
      GRect(4, hint_y, b.size.w - 8, hint_h),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
  if (show_bottom_bar) {
    draw_bottom_bar(gctx, bottom_bar_rect_for_bounds(b));
  }
}

// Commits the currently selected duration and advances past the dial window.
// Shared by the physical-button confirm (dial_select, once all fields are set)
// and the touch dial, which reports a duration in one shot on finger liftoff.
static void dial_confirm(void) {
  if (s_dial_existing_duration_edit && s_detail_style == DSTYLE_LONG_EXISTING) {
    apply_overwrite_only(s_detail_idx, s_detail_edit_secs, NULL);
    s_dial_existing_duration_edit = false;
    if (window_stack_contains_window(s_dial_window)) {
      window_stack_remove(s_dial_window, true);
    }
    return;
  }
  s_dial_advancing = true;
  if (s_detail_style == DSTYLE_LONG_NEW) { s_new_flow_open_label_after_dial = true; }
  open_detail_window(s_detail_idx, s_detail_style);
  if (window_stack_contains_window(s_dial_window)) {
    window_stack_remove(s_dial_window, false);
  }
}

static void dial_select(ClickRecognizerRef rec, void *ctx) {
  idle_reset();
  dial_touch_enable(true);
  if (s_dial_field < 2) {
    s_dial_field++;
    if (s_dial_layer) { layer_mark_dirty(s_dial_layer); }
    return;
  }
  dial_confirm();
}

static void dial_back(ClickRecognizerRef rec, void *ctx) {
  idle_reset();
  dial_touch_enable(true);
  if (s_dial_existing_duration_edit && s_detail_style == DSTYLE_LONG_EXISTING) {
    if (s_dial_field > 0) {
      s_dial_field--;
      if (s_dial_layer) { layer_mark_dirty(s_dial_layer); }
      return;
    }
    s_dial_existing_duration_edit = false;
    if (window_stack_contains_window(s_dial_window)) {
      window_stack_remove(s_dial_window, false);
    }
    if (s_detail_window && window_stack_contains_window(s_detail_window)) {
      window_stack_remove(s_detail_window, true);
    }
    return;
  }
  if (s_dial_field > 0) {
    s_dial_field--;
    if (s_dial_layer) { layer_mark_dirty(s_dial_layer); }
    return;
  }
  if (window_stack_contains_window(s_dial_window)) {
    window_stack_remove(s_dial_window, true);
  }
}

static void dial_up(ClickRecognizerRef rec, void *ctx) {
  idle_reset();
  dial_touch_enable(true);
  dial_adjust_current(+1);
}

static void dial_down(ClickRecognizerRef rec, void *ctx) {
  idle_reset();
  dial_touch_enable(true);
  dial_adjust_current(-1);
}

static void dial_up_long_begin(ClickRecognizerRef rec, void *ctx) {
  dial_hold_begin_common(+1);
}

static void dial_up_long_end(ClickRecognizerRef rec, void *ctx) {
  dial_hold_end_common();
}

static void dial_down_long_begin(ClickRecognizerRef rec, void *ctx) {
  dial_hold_begin_common(-1);
}

static void dial_down_long_end(ClickRecognizerRef rec, void *ctx) {
  dial_hold_end_common();
}

static void dial_select_long(ClickRecognizerRef rec, void *ctx) {
  if (s_detail_style != DSTYLE_LONG_EXISTING || s_dial_existing_duration_edit) { return; }
  idle_reset();
  open_delete_confirm(false, DELCONF_ACTION);
}

static void dial_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, dial_select);
  window_single_click_subscribe(BUTTON_ID_BACK, dial_back);
  window_single_click_subscribe(BUTTON_ID_UP, dial_up);
  window_single_click_subscribe(BUTTON_ID_DOWN, dial_down);
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, dial_select_long, NULL);
  window_long_click_subscribe(BUTTON_ID_UP, 200, dial_up_long_begin, dial_up_long_end);
  window_long_click_subscribe(BUTTON_ID_DOWN, 200, dial_down_long_begin, dial_down_long_end);
}

static void dial_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_dial_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_dial_layer, dial_update_proc);
  layer_add_child(root, s_dial_layer);
}

static void dial_window_unload(Window *w) {
  dial_hold_cancel();
  if (s_dial_layer) { layer_destroy(s_dial_layer); s_dial_layer = NULL; }
}

static void dl_rebuild_actions(void) {
  if (s_detail_idx < 0 || s_detail_idx >= s_count) { s_detail_act_count = 0; return; }
  Timer *t = &s_timers[s_detail_idx];
  s_detail_act_count = tc_detail_actions(t->state, s_detail_acts);
}

static const char *dl_legacy_action_label(DetailAction a) {
  switch (a) {
    case DACT_STOP:       return "Stop";
    case DACT_PAUSE:      return "Pause";
    case DACT_START:
      if (s_detail_idx >= 0 && s_detail_idx < s_count && s_timers[s_detail_idx].state == TS_PAUSED) {
        return "Continue";
      }
      return "Start";
    case DACT_PLUS:       return "+1 min";
    case DACT_MINUS:      return "-1 min";
    case DACT_DELETE:     return "Delete";
  }
  return "";
}

static uint16_t dl_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  if (s_detail_style == DSTYLE_LEGACY) {
    dl_rebuild_actions();
    return (uint16_t)s_detail_act_count;
  }
  return 3;
}
static int16_t dl_cell_height(MenuLayer *ml, MenuIndex *ci, void *ctx) { return 34; }
static int16_t dl_header_height(MenuLayer *ml, uint16_t section, void *ctx) { return 32; }

// Header: timer name (left) + edited time (right); time only if unnamed.
static void dl_draw_header(GContext *gctx, const Layer *cell, uint16_t section, void *ctx) {
  if (s_detail_idx < 0 || s_detail_idx >= s_count) { return; }
  Timer *t = &s_timers[s_detail_idx];
  int32_t shown = (s_detail_style == DSTYLE_LEGACY) ? tc_remaining_now(t, now_s()) : s_detail_edit_secs;
  char rem_head[36];
  tc_format_remaining(rem_head, sizeof(rem_head), shown);
  const char *title = t->name[0] ? t->name : "<No label>";
  GRect b = layer_get_bounds(cell);
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  graphics_context_set_text_color(gctx, GColorBlack);
  if (s_detail_style == DSTYLE_LONG_EXISTING) {
    graphics_draw_text(gctx, title, f, GRect(4, 3, b.size.w - 92, 26),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(gctx, rem_head, f, GRect(4, 3, b.size.w - 8, 26),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    return;
  }
  if (s_detail_style == DSTYLE_LONG_NEW) {
    graphics_draw_text(gctx, title, f, GRect(4, 3, b.size.w - 92, 26),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(gctx, rem_head, f, GRect(4, 3, b.size.w - 8, 26),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    return;
  }
  graphics_draw_text(gctx, title, f, GRect(4, 3, b.size.w - 92, 26),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(gctx, rem_head, f, GRect(4, 3, b.size.w - 8, 26),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

static void dl_draw_row(GContext *gctx, const Layer *cell, MenuIndex *ci, void *ctx) {
  const char *label = "";
  static char finish_label[24];
  if (s_detail_style == DSTYLE_LEGACY) {
    if (ci->row >= s_detail_act_count) { return; }
    label = dl_legacy_action_label(s_detail_acts[ci->row]);
  } else {
    if (s_detail_style == DSTYLE_LONG_EXISTING) {
      if (ci->row == 0) { label = "Edit duration"; }
      else if (ci->row == 1) { label = "Edit label"; }
      else if (ci->row == 2) {
        bool del = (s_detail_idx >= 0 && s_detail_idx < s_count)
          ? s_delete_on_finish[s_detail_idx] : false;
        snprintf(finish_label, sizeof(finish_label), "After finished: %s", del ? "Delete" : "Save");
        label = finish_label;
      }
      else { return; }
    } else {
      return;
    }
  }
  GRect b = layer_get_bounds(cell);
  graphics_draw_text(gctx, label,
    fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
    GRect(6, (b.size.h - 26) / 2, b.size.w - 12, 26),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void start_as_new(int32_t secs, bool save_to_phone, const char *name); // defined below
static void apply_overwrite_only(int idx, int32_t secs, const char *name);    // defined below
static void send_add_timer(int32_t secs, const char *name);          // defined below (phone sync)
static void send_update_timer(int32_t idx, int32_t secs, const char *name); // defined below (phone sync)
static void create_new_timer(void);                // defined below ("+ New timer" row)
static void open_delete_confirm(bool stay_on_detail_on_cancel, DelConfirmKind kind); // defined below (delete path)
static void send_delete_timer(int32_t idx);        // defined below (delete path)
static void show_start_confirmation(int idx);      // defined below (auto-return tail)
static void open_label_input_for_new_timer(int idx); // defined below

static void dl_select(MenuLayer *ml, MenuIndex *ci, void *ctx) {
  idle_reset();
  int idx = s_detail_idx;
  if (idx < 0 || idx >= s_count) { return; }
  if (s_detail_style == DSTYLE_LEGACY) {
    dl_rebuild_actions();
    if (ci->row >= s_detail_act_count) { return; }
    DetailAction a = s_detail_acts[ci->row];
    Timer *t = &s_timers[idx];
    switch (a) {
      case DACT_PAUSE:
        tc_pause(t, now_s());
        persist_all(); rearm_wakeup(); ensure_ticking();
        reload_ui();
        select_timer_row(idx);
        window_stack_remove(s_detail_window, true);
        break;
      case DACT_START:
        {
        bool was_paused = (t->state == TS_PAUSED);
        int32_t base = t->remaining;
        if (base < 1) { base = t->duration; }
        start_with_secs(t, launch_adjust_start_secs_for_timer(t, base));
        ensure_unnamed_star(idx);
        if (idx == s_new_timer_idx) {
          send_add_timer(t->duration, t->name);
          s_new_timer_idx = -1;
        }
        bool fired = finish_start_tail();
        if (fired) { break; }
        if (was_paused) { window_stack_remove(s_detail_window, true); }
        else if (s_auto_return) { show_start_confirmation(idx); }
        else { menu_layer_reload_data(s_detail_menu); }
        break;
        }
      case DACT_STOP: {
        bool was_delete_on_finish = s_delete_on_finish[idx];
        if (was_delete_on_finish) { remove_timer_at(idx); }
        else { tc_reset(t, now_s()); }
        persist_all(); rearm_wakeup(); reload_ui();
        if (!was_delete_on_finish) { select_timer_row(idx); }
        if (!was_delete_on_finish && s_auto_return) { close_to_watchface(); }
        else { window_stack_remove(s_detail_window, true); }
        break;
      }
      case DACT_PLUS:
      case DACT_MINUS: {
        int32_t secs = (a == DACT_PLUS) ? 60 : -60;
        if (idx == s_new_timer_idx && t->state == TS_IDLE) {
          int32_t d = t->duration + secs;
          if (d < 60) { d = 60; }
          t->duration = d; t->remaining = d; t->last_used = now_s();
          reload_ui(); menu_layer_reload_data(s_detail_menu);
          break;
        }
        if (t->state == TS_RUNNING || t->state == TS_PAUSED) {
          tc_add(t, secs, now_s());
        } else {
          int32_t r = t->remaining + secs;
          if (r < 60) { r = 60; }
          t->remaining = r;
          t->last_used = now_s();
        }
        persist_all(); rearm_wakeup(); ensure_ticking();
        reload_ui(); menu_layer_reload_data(s_detail_menu);
        break;
      }
      case DACT_DELETE:
        open_delete_confirm(false, DELCONF_ACTION);
        break;
    }
    return;
  }
  if (s_detail_style == DSTYLE_LONG_EXISTING) {
    if (ci->row == 0) { // Edit duration
      s_dial_existing_duration_edit = true;
      s_detail_advancing = true;
      open_dial_window(idx, DSTYLE_LONG_EXISTING);
      return;
    }
    if (ci->row == 1) { // Edit label
      open_label_input_for_new_timer(idx);
      return;
    }
    if (ci->row == 2) { // After finished: Delete/Save (toggle)
      Timer *t = &s_timers[idx];
      s_delete_on_finish[idx] = !s_delete_on_finish[idx];
      if (s_delete_on_finish[idx]) {
        send_delete_timer(idx);   // now watch-local only: drop it from the phone
      } else {
        t->custom = true;
        send_add_timer(t->duration, t->name);   // now "kept": add it to the phone
      }
      persist_all();
      if (s_detail_menu) { menu_layer_reload_data(s_detail_menu); }
    }
    return;
  }
}

static void detail_up_click(ClickRecognizerRef rec, void *ctx) {
  idle_reset();
  if (!s_detail_menu) { return; }
  menu_layer_set_selected_next(s_detail_menu, true, MenuRowAlignNone, true);
}

static void detail_down_click(ClickRecognizerRef rec, void *ctx) {
  idle_reset();
  if (!s_detail_menu) { return; }
  menu_layer_set_selected_next(s_detail_menu, false, MenuRowAlignNone, true);
}

static void detail_select_click(ClickRecognizerRef rec, void *ctx) {
  if (!s_detail_menu) { return; }
  MenuIndex sel = menu_layer_get_selected_index(s_detail_menu);
  dl_select(s_detail_menu, &sel, NULL);
}

static void detail_back_click(ClickRecognizerRef rec, void *ctx) {
  idle_reset();
  int idx = s_detail_idx;
  // Leaving the menu after changing After-finished from Save to Delete during this
  // visit: confirm before exiting. A stopped timer has no future finish/stop
  // transition left to apply it (Select deletes now); a running timer will still
  // apply it naturally when it finishes/stops (Select just leaves). If it was
  // already Delete when the menu opened, there's nothing new to confirm.
  if (s_detail_style == DSTYLE_LONG_EXISTING && idx >= 0 && idx < s_count
      && s_delete_on_finish[idx] && s_detail_del_on_finish_was_save) {
    TimerState st = s_timers[idx].state;
    if (st == TS_IDLE || st == TS_DONE) {
      open_delete_confirm(true, DELCONF_EXIT_STOPPED);
      return;
    }
    if (st == TS_RUNNING) {
      open_delete_confirm(true, DELCONF_EXIT_RUNNING);
      return;
    }
  }
  if (s_detail_window && window_stack_contains_window(s_detail_window)) {
    window_stack_remove(s_detail_window, true);
  }
}

static void detail_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, detail_select_click);
  window_single_click_subscribe(BUTTON_ID_UP, detail_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, detail_down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, detail_back_click);
}

static void new_timer_label_result(const char *text, void *context) {
  int idx = (int)(intptr_t)context;
  s_label_target_idx = -1;
  s_detail_advancing = false;
  if (idx < 0 || idx >= s_count) { return; }
  if (s_label_return_style == DSTYLE_LONG_NEW && !text) {
    if (s_new_timer_idx >= 0 && s_new_timer_idx < s_count
        && s_new_timer_idx == idx && s_timers[s_new_timer_idx].state == TS_IDLE) {
      remove_timer_at(s_new_timer_idx);
      s_new_timer_idx = -1;
      reload_ui();
    }
    if (s_detail_window && window_stack_contains_window(s_detail_window)) {
      window_stack_remove(s_detail_window, true);
    }
    return;
  }
  if (s_label_return_style == DSTYLE_LONG_EXISTING) {
    if (text) {
      apply_overwrite_only(idx, s_timers[idx].duration, text);
    } else if (s_detail_window && window_stack_contains_window(s_detail_window)) {
      window_stack_remove(s_detail_window, true);
    }
    return;
  }
  if (text) {
    Timer *t = &s_timers[idx];
    if (text[0]) {
      strncpy(t->name, text, NAME_LEN);
      t->name[NAME_LEN] = '\0';
    } else {
      t->name[0] = '\0';
    }
    t->last_used = now_s();
    if (s_label_return_style == DSTYLE_LONG_NEW) {
      // New-timer flow: no Run/Save menu. Starts immediately unless "Run timer
      // when created" is off, in which case it's left idle for a manual start.
      // Phone-sync it when the effective mode is "Save" (After finished).
      t->duration = s_detail_edit_secs;
      t->remaining = s_detail_edit_secs;
      t->custom = true;
      bool fired = false;
      if (s_run_on_create) {
        start_with_secs(t, launch_adjust_start_secs(s_detail_edit_secs));
        fired = finish_start_tail();
      }
      assign_unnamed_star_for_duration(idx, t->duration);
      if (!s_delete_on_finish[idx]) { send_add_timer(t->duration, t->name); }
      s_new_timer_idx = -1;
      if (fired) { return; }
      select_timer_row(idx);
      if (s_run_on_create && s_auto_return) { show_start_confirmation(idx); }
      else if (s_detail_window && window_stack_contains_window(s_detail_window)) {
        window_stack_remove(s_detail_window, true);
      }
      return;
    }
    assign_unnamed_star_for_duration(idx, t->duration);
    if (s_detail_window && window_stack_contains_window(s_detail_window)) {
      s_detail_idx = idx;
      s_detail_style = s_label_return_style;
      if (s_detail_menu) { menu_layer_reload_data(s_detail_menu); }
    }
    reload_ui();
  }
}

static void open_label_input_for_new_timer(int idx) {
  if (idx < 0 || idx >= s_count) { return; }
  s_label_target_idx = idx;
  s_label_return_style = s_detail_style;
  const char *initial = (s_timers[idx].name[0] != 0) ? s_timers[idx].name : NULL;
  s_detail_advancing = true;
  multitap_keyboard_window_push_ex(new_timer_label_result, initial, NAME_LEN, (void *)(intptr_t)idx);
}

static void new_flow_open_label_cb(void *ctx) {
  (void)ctx;
  s_new_flow_label_timer = NULL;
  int idx = s_new_flow_label_idx;
  s_new_flow_label_idx = -1;
  if (idx >= 0 && idx < s_count && s_detail_style == DSTYLE_LONG_NEW) {
    open_label_input_for_new_timer(idx);
  }
}

static void main_bottom_bar_update_proc(Layer *layer, GContext *gctx) {
  draw_bottom_bar(gctx, layer_get_bounds(layer));
}

static void detail_bottom_bar_update_proc(Layer *layer, GContext *gctx) {
  draw_bottom_bar(gctx, layer_get_bounds(layer));
}

static void detail_apply_layout(void) {
  if (!s_detail_window || !s_detail_menu) { return; }
  Layer *root = window_get_root_layer(s_detail_window);
  GRect bounds = layer_get_bounds(root);
  bool show_bar = detail_style_has_bottom_bar();
  GRect menu_bounds = bounds;
  if (show_bar) { menu_bounds.size.h = bottom_bar_top_for_bounds(bounds); }
  layer_set_frame(menu_layer_get_layer(s_detail_menu), menu_bounds);
  if (s_detail_bottom_bar_layer) {
    layer_set_frame(s_detail_bottom_bar_layer, bottom_bar_rect_for_bounds(bounds));
    layer_set_hidden(s_detail_bottom_bar_layer, !show_bar);
    if (show_bar) { layer_mark_dirty(s_detail_bottom_bar_layer); }
  }
}

static void detail_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  GRect menu_bounds = bounds;
  if (detail_style_has_bottom_bar()) { menu_bounds.size.h = bottom_bar_top_for_bounds(bounds); }
  s_detail_menu = menu_layer_create(menu_bounds);
  menu_layer_set_callbacks(s_detail_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = dl_num_rows,
    .get_cell_height = dl_cell_height,
    .get_header_height = dl_header_height,
    .draw_header = dl_draw_header,
    .draw_row = dl_draw_row,
    .select_click = dl_select,
  });
  menu_layer_set_normal_colors(s_detail_menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_detail_menu, GColorBlack, GColorWhite);
  window_set_click_config_provider(w, detail_click_config);
  layer_add_child(root, menu_layer_get_layer(s_detail_menu));
  s_detail_bottom_bar_layer = layer_create(bottom_bar_rect_for_bounds(bounds));
  layer_set_update_proc(s_detail_bottom_bar_layer, detail_bottom_bar_update_proc);
  layer_set_hidden(s_detail_bottom_bar_layer, !detail_style_has_bottom_bar());
  layer_add_child(root, s_detail_bottom_bar_layer);
}

static void detail_window_unload(Window *w) {
  if (s_detail_bottom_bar_layer) { layer_destroy(s_detail_bottom_bar_layer); s_detail_bottom_bar_layer = NULL; }
  menu_layer_destroy(s_detail_menu); s_detail_menu = NULL;
}

static void del_update_proc(Layer *layer, GContext *gctx) {
  GRect b = layer_get_bounds(layer);
  int cy = b.size.h / 2;
  graphics_context_set_text_color(gctx, GColorBlack);
  bool exit_running = (s_del_confirm_kind == DELCONF_EXIT_RUNNING);
  const char *header = exit_running ? "Delete after\nfinished?" : "Delete?";
  const char *instructions = exit_running ? "SELECT confirm\nBACK cancel" : "SELECT delete\nBACK cancel";
  graphics_draw_text(gctx, header, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
    GRect(4, cy - 70, b.size.w - 8, 60), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  graphics_draw_text(gctx, s_del_name, fonts_get_system_font(FONT_KEY_GOTHIC_24),
    GRect(4, cy - 6, b.size.w - 8, 30), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(gctx, instructions, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(4, cy + 28, b.size.w - 8, 44), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void del_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_del_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_del_layer, del_update_proc);
  layer_add_child(root, s_del_layer);
}

static void del_window_unload(Window *w) {
  if (s_del_layer) { layer_destroy(s_del_layer); s_del_layer = NULL; }
}

// Select confirms: tell the phone, remove locally, then drop both the confirm and
// the detail window so we land back on the LIST (delete is management, not an exit).
static void del_confirm_select(ClickRecognizerRef rec, void *ctx) {
  int idx = s_detail_idx;
  if (s_del_confirm_kind == DELCONF_EXIT_RUNNING) {
    // delete-on-finish is already set; the timer is still running, so deletion
    // applies naturally at its next finish/stop. Just leave.
    window_stack_remove(s_del_window, false);
    s_detail_idx = -1;
    window_stack_remove(s_detail_window, true);
    return;
  }
  if (idx >= 0 && idx < s_count) {
    if (idx == s_new_timer_idx || s_delete_on_finish[idx]) { s_new_timer_idx = -1; }  // never synced -> no phone delete
    else { send_delete_timer(idx); }
    remove_timer_at(idx);
    persist_all(); rearm_wakeup(); reload_ui();
  }
  s_detail_idx = -1;
  window_stack_remove(s_del_window, false);
  if (s_dial_window && window_stack_contains_window(s_dial_window)) {
    window_stack_remove(s_dial_window, false);
  }
  window_stack_remove(s_detail_window, true);
}

static void del_confirm_back(ClickRecognizerRef rec, void *ctx) {
  bool stay_on_detail = s_del_confirm_stay_on_detail;
  s_del_confirm_stay_on_detail = false;
  window_stack_remove(s_del_window, false);
  if (stay_on_detail) { return; }
  bool exit_edit_flow = (s_detail_style == DSTYLE_LONG_EXISTING);
  if (!exit_edit_flow) { return; }
  if (s_dial_window && window_stack_contains_window(s_dial_window)) {
    window_stack_remove(s_dial_window, false);
  }
  if (s_detail_window && window_stack_contains_window(s_detail_window)) {
    window_stack_remove(s_detail_window, true);
  }
}

static void del_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, del_confirm_select);
  window_single_click_subscribe(BUTTON_ID_BACK, del_confirm_back);
}

static void open_delete_confirm(bool stay_on_detail_on_cancel, DelConfirmKind kind) {
  if (s_detail_idx < 0 || s_detail_idx >= s_count) { return; }
  s_del_confirm_stay_on_detail = stay_on_detail_on_cancel;
  s_del_confirm_kind = kind;
  Timer *t = &s_timers[s_detail_idx];
  if (t->name[0]) {
    snprintf(s_del_name, sizeof(s_del_name), "%s", t->name);
  } else {
    tc_format_remaining(s_del_name, sizeof(s_del_name), tc_remaining_now(t, now_s()));
  }
  if (!s_del_window) {
    s_del_window = window_create();
    window_set_window_handlers(s_del_window, (WindowHandlers){
      .load = del_window_load, .unload = del_window_unload });
    window_set_click_config_provider(s_del_window, del_click_config);
  }
  s_detail_advancing = true;
  window_stack_push(s_del_window, true);
}

// Leaving the detail window: stop the idle timer AND discard an un-started draft
// new timer (BACK without Start/Save). A committed timer has s_new_timer_idx == -1,
// so this never discards a started/saved timer.
static void detail_disappear(Window *w) {
  idle_cancel();
  bool advancing = s_detail_advancing;
  s_detail_advancing = false;
  if (advancing) { return; }
  if (s_new_timer_idx >= 0 && s_new_timer_idx == s_detail_idx
      && s_new_timer_idx < s_count && s_timers[s_new_timer_idx].state == TS_IDLE) {
    remove_timer_at(s_new_timer_idx);
    s_new_timer_idx = -1;
    reload_ui();
  }
}

static void detail_appear(Window *w) {
  idle_appear(w);
  if (!s_new_flow_open_label_after_dial) { return; }
  s_new_flow_open_label_after_dial = false;
  if (s_detail_style == DSTYLE_LONG_NEW && s_detail_idx >= 0 && s_detail_idx < s_count) {
    s_new_flow_label_idx = s_detail_idx;
    if (s_new_flow_label_timer) { app_timer_cancel(s_new_flow_label_timer); }
    s_new_flow_label_timer = app_timer_register(10, new_flow_open_label_cb, NULL);
  }
}

static void dial_appear(Window *w) {
  idle_appear(w);
  dial_touch_create(window_get_root_layer(w), dial_touch_selected);
  dial_touch_enable(true);
}

static void dial_disappear(Window *w) {
  idle_cancel();
  dial_touch_enable(false);
  dial_touch_destroy();
  dial_hold_cancel();
  if (s_dial_touch_confirm_timer) {
    app_timer_cancel(s_dial_touch_confirm_timer);
    s_dial_touch_confirm_timer = NULL;
  }
  bool advancing = s_dial_advancing;
  s_dial_advancing = false;
  if (!window_stack_contains_window(s_dial_window)) { s_dial_existing_duration_edit = false; }
  if (advancing) { return; }
  if (s_new_timer_idx >= 0 && s_new_timer_idx == s_detail_idx
      && s_new_timer_idx < s_count && s_timers[s_new_timer_idx].state == TS_IDLE) {
    remove_timer_at(s_new_timer_idx);
    s_new_timer_idx = -1;
    reload_ui();
  }
}

static void open_detail_window(int timer_idx, DetailStyle style) {
  s_detail_idx = timer_idx;
  s_detail_style = style;
  if (timer_idx >= 0 && timer_idx < s_count) {
    Timer *t = &s_timers[timer_idx];
    if (style == DSTYLE_LEGACY) {
      int32_t rem = tc_remaining_now(t, now_s());
      s_detail_edit_secs = rem >= 1 ? rem : t->duration;
    } else if (style == DSTYLE_LONG_EXISTING) {
      s_detail_edit_secs = t->duration;
      s_detail_del_on_finish_was_save = !s_delete_on_finish[timer_idx];
    }
    if (s_detail_edit_secs < 0) { s_detail_edit_secs = 0; }
  }
  if (!s_detail_window) {
    s_detail_window = window_create();
    window_set_window_handlers(s_detail_window, (WindowHandlers){
      .load = detail_window_load, .unload = detail_window_unload,
      .appear = detail_appear, .disappear = detail_disappear });
  }
  // Idempotent: if it is already on the stack (e.g. it was open under the alarm when
  // a timer expired), just refresh it instead of pushing it a second time.
  if (window_stack_contains_window(s_detail_window)) {
    detail_apply_layout();
    if (s_detail_menu) { menu_layer_reload_data(s_detail_menu); }
  } else {
    window_stack_push(s_detail_window, true);
  }
}

static void open_dial_window(int timer_idx, DetailStyle style) {
  s_detail_idx = timer_idx;
  s_detail_style = style;
  if (style != DSTYLE_LONG_EXISTING) { s_dial_existing_duration_edit = false; }
  s_dial_field = 1; // minute first on all platforms
  if (timer_idx >= 0 && timer_idx < s_count) {
    Timer *t = &s_timers[timer_idx];
    if (style == DSTYLE_LONG_NEW) { s_detail_edit_secs = 60; }
    else { s_detail_edit_secs = t->duration; } // existing timer edit: start from current duration
    if (s_detail_edit_secs < 0) { s_detail_edit_secs = 0; }
  }
  if (!s_dial_window) {
    s_dial_window = window_create();
    window_set_window_handlers(s_dial_window, (WindowHandlers){
      .load = dial_window_load, .unload = dial_window_unload,
      .appear = dial_appear, .disappear = dial_disappear });
    window_set_click_config_provider(s_dial_window, dial_click_config);
  }
  if (window_stack_contains_window(s_dial_window)) {
    if (s_dial_layer) { layer_mark_dirty(s_dial_layer); }
  } else {
    window_stack_push(s_dial_window, true);
  }
}

static void dial_touch_confirm_cb(void *data) {
  s_dial_touch_confirm_timer = NULL;
  dial_confirm();
}

static void dial_touch_selected(uint8_t hours, uint8_t minutes, uint8_t seconds) {
  int h = (int)hours;
  int m = (int)minutes;
  int s = (int)seconds;
  if (h > 100) { h = 100; }
  m = ((m % 60) + 60) % 60;
  s = ((s % 60) + 60) % 60;
  s_detail_edit_secs = h * 3600 + m * 60 + s;
  if (s_dial_layer) { layer_mark_dirty(s_dial_layer); }
  // New-timer and existing-timer-duration-edit flows: the touch dial reports
  // a duration in one shot on finger liftoff, so that alone is enough to
  // confirm - unlike the physical dial, which needs an explicit SELECT once
  // all hour/minute/second fields have been stepped through.
  //
  // This callback fires from inside the touch dial's own touch-event handler
  // (touch_dial/touch.c's handle_touch_event), which still has work left to
  // do after invoking it (clearing its selection state, tearing down its
  // animation). Confirming synchronously here would tear down the dial
  // window - and with it the touch dial's layer, via dial_disappear's
  // dial_touch_destroy() - out from under that still-executing handler,
  // a use-after-free that only reliably crashed on real hardware. Defer the
  // confirm to a fresh app timer so it runs after that handler has returned.
  if (s_detail_style == DSTYLE_LONG_NEW
      || (s_detail_style == DSTYLE_LONG_EXISTING && s_dial_existing_duration_edit)) {
    if (s_dial_touch_confirm_timer) { app_timer_cancel(s_dial_touch_confirm_timer); }
    s_dial_touch_confirm_timer = app_timer_register(10, dial_touch_confirm_cb, NULL);
  }
}

static void main_touch_selected(uint8_t hours, uint8_t minutes, uint8_t seconds) {
  int h = (int)hours;
  int m = (int)minutes;
  int s = (int)seconds;
  if (h > 100) { h = 100; }
  m = ((m % 60) + 60) % 60;
  s = ((s % 60) + 60) % 60;
  int32_t secs = h * 3600 + m * 60 + s;
  start_as_new(secs, false, NULL);
}

// ---- MenuLayer callbacks ----
typedef enum {
  ML_ROW_TIMER_PRIMARY = 0,
  ML_ROW_TIMER_DETAIL,
  ML_ROW_NEW
} MlRowKind;

typedef struct {
  MlRowKind kind;
  int timer_idx; // valid for TIMER_* rows
} MlRowInfo;

#define ML_ROW_H_PRIMARY 32
#define ML_ROW_H_DETAIL  28
static int16_t ml_row_height_for_kind(MlRowKind kind) {
  return (kind == ML_ROW_TIMER_DETAIL) ? ML_ROW_H_DETAIL : ML_ROW_H_PRIMARY;
}

static bool ml_timer_shows_detail(int idx, int selected_idx) {
  (void)selected_idx;
  if (idx < 0 || idx >= s_count) { return false; }
  TimerState st = s_timers[idx].state;
  return (st == TS_RUNNING || st == TS_PAUSED);
}

static bool ml_row_info_for(uint16_t row, int selected_idx, MlRowInfo *out) {
  uint16_t v = 0;
  for (int i = 0; i < s_count; i++) {
    int idx = s_order[i];
    if (v == row) { out->kind = ML_ROW_TIMER_PRIMARY; out->timer_idx = idx; return true; }
    v++;
    if (ml_timer_shows_detail(idx, selected_idx)) {
      if (v == row) { out->kind = ML_ROW_TIMER_DETAIL; out->timer_idx = idx; return true; }
      v++;
    }
  }
  if (v == row) { out->kind = ML_ROW_NEW; out->timer_idx = -1; return true; }
  return false;
}

static bool ml_is_item_boundary_row(uint16_t row, int selected_idx) {
  MlRowInfo cur;
  MlRowInfo next;
  if (!ml_row_info_for(row, selected_idx, &cur)) { return false; }
  if (cur.kind == ML_ROW_NEW) { return false; }
  if (!ml_row_info_for((uint16_t)(row + 1), selected_idx, &next)) { return false; }
  if (next.kind == ML_ROW_NEW) { return true; }
  return cur.timer_idx != next.timer_idx;
}

static int ml_row_for_timer_primary(int timer_idx, int selected_idx) {
  int v = 0;
  for (int i = 0; i < s_count; i++) {
    int idx = s_order[i];
    if (idx == timer_idx) { return v; }
    v++;
    if (ml_timer_shows_detail(idx, selected_idx)) { v++; }
  }
  return -1;
}

static int ml_order_pos_for_timer(int timer_idx) {
  for (int i = 0; i < s_count; i++) {
    if (s_order[i] == timer_idx) { return i; }
  }
  return -1;
}

static int ml_row_for_new(int selected_idx) {
  int v = 0;
  for (int i = 0; i < s_count; i++) {
    int idx = s_order[i];
    v++;
    if (ml_timer_shows_detail(idx, selected_idx)) { v++; }
  }
  return v;
}

static int ml_row_top_for(uint16_t row, int selected_idx) {
  int y = 0;
  for (uint16_t r = 0; r < row; r++) {
    MlRowInfo ri;
    if (!ml_row_info_for(r, selected_idx, &ri)) { break; }
    y += ml_row_height_for_kind(ri.kind);
  }
  return y;
}

static bool ml_block_for_selection(int selected_idx, int *out_y, int *out_h) {
  if (selected_idx == -1) {
    int row = ml_row_for_new(selected_idx);
    if (row < 0) { return false; }
    if (out_y) { *out_y = ml_row_top_for((uint16_t)row, selected_idx); }
    if (out_h) { *out_h = ML_ROW_H_PRIMARY; }
    return true;
  }
  int row = ml_row_for_timer_primary(selected_idx, selected_idx);
  if (row < 0) { return false; }
  int h = ML_ROW_H_PRIMARY;
  if (ml_timer_shows_detail(selected_idx, selected_idx)) { h += ML_ROW_H_DETAIL; }
  if (out_y) { *out_y = ml_row_top_for((uint16_t)row, selected_idx); }
  if (out_h) { *out_h = h; }
  return true;
}

static bool ml_current_highlight_rect(int *out_y, int *out_h) {
  return ml_block_for_selection(s_menu_selected_timer_idx, out_y, out_h);
}

static void ml_stop_animation(void) {
  // Selection highlight movement is intentionally immediate (no interpolation).
}

static void ml_scroll_item_bounds_into_view(int item_y, int item_h) {
  if (!s_menu) { return; }
  ScrollLayer *sl = menu_layer_get_scroll_layer(s_menu);
  if (!sl) { return; }
  GPoint off = scroll_layer_get_content_offset(sl);
  GRect vb = layer_get_bounds(menu_layer_get_layer(s_menu));
  int vis_top = -off.y;
  int vis_bottom = vis_top + vb.size.h - 1;
  int item_bottom = item_y + item_h;
  int target_top = vis_top;
  if (item_y < vis_top) { target_top = item_y; }
  else if (item_bottom > vis_bottom) { target_top = item_bottom - vb.size.h - 1; }
  else { return; }
  GSize cs = scroll_layer_get_content_size(sl);
  int max_top = cs.h - vb.size.h;
  if (max_top < 0) { max_top = 0; }
  if (target_top < 0) { target_top = 0; }
  if (target_top > max_top) { target_top = max_top; }
  if (target_top == vis_top) { return; }
  scroll_layer_set_content_offset(sl, GPoint(0, -target_top), false);
}

static void ml_row_colors(const Timer *t, bool selected, GColor *bg, GColor *fg) {
  if (selected) {
    *fg = GColorWhite;
    switch (t->state) {
      case TS_RUNNING: *bg = PBL_IF_COLOR_ELSE(GColorDarkGreen, GColorBlack); break;
      case TS_PAUSED:  *bg = PBL_IF_COLOR_ELSE(GColorArmyGreen, GColorBlack); break;
      case TS_DONE:    *bg = PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorBlack); break;
      default:         *bg = GColorBlack; break;   // TS_IDLE
    }
  } else {
    *fg = GColorBlack;
    switch (t->state) {
      case TS_RUNNING: *bg = PBL_IF_COLOR_ELSE(GColorMediumSpringGreen, GColorWhite); break;
      case TS_PAUSED:  *bg = PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite); break;
      case TS_DONE:    *bg = PBL_IF_COLOR_ELSE(GColorSunsetOrange, GColorWhite); break;
      default:         *bg = GColorWhite; break;   // TS_IDLE
    }
  }
}

static void ml_draw_state_icon(GContext *gctx, int x, int y, TimerState st, GColor color) {
  graphics_context_set_fill_color(gctx, color);
  if (st == TS_PAUSED) {
    graphics_fill_rect(gctx, GRect(x, y, 3, 12), 0, GCornerNone);
    graphics_fill_rect(gctx, GRect(x + 5, y, 3, 12), 0, GCornerNone);
    return;
  }
  if (st == TS_RUNNING) {
    const int h = 12;
    const int w = 10;
    for (int row = 0; row < h; row++) {
      int d = (row <= (h / 2)) ? ((h / 2) - row) : (row - (h / 2));
      int span = w - (d * w) / (h / 2 + 1);
      if (span < 1) { span = 1; }
      graphics_fill_rect(gctx, GRect(x, y + row, span, 1), 0, GCornerNone);
    }
    return;
  }
  // Stopped: simple square.
  graphics_fill_rect(gctx, GRect(x, y + 1, 10, 10), 0, GCornerNone);
}

static uint16_t ml_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  uint16_t rows = (uint16_t)(s_count + 1); // primary rows + trailing "New timer"
  for (int i = 0; i < s_count; i++) {
    if (ml_timer_shows_detail(s_order[i], s_menu_selected_timer_idx)) { rows++; }
  }
  return rows;
}

static int16_t ml_cell_height(MenuLayer *ml, MenuIndex *ci, void *ctx) {
  MlRowInfo info;
  if (!ml_row_info_for(ci->row, s_menu_selected_timer_idx, &info)) { return ML_ROW_H_PRIMARY; }
  return ml_row_height_for_kind(info.kind);
}

static void ml_draw_row(GContext *gctx, const Layer *cell, MenuIndex *ci, void *ctx) {
  MlRowInfo info;
  if (!ml_row_info_for(ci->row, s_menu_selected_timer_idx, &info)) { return; }
  if (info.kind == ML_ROW_NEW) {
    GRect b = layer_get_bounds(cell);
    bool selected = (s_menu_selected_timer_idx == -1);
    graphics_context_set_fill_color(gctx, GColorWhite);
    graphics_fill_rect(gctx, b, 0, GCornerNone);
    int hy, hh;
    if (ml_current_highlight_rect(&hy, &hh)) {
      int row_top = ml_row_top_for(ci->row, s_menu_selected_timer_idx);
      int row_bottom = row_top + b.size.h;
      int h_top = hy;
      int h_bottom = hy + hh;
      int ov_top = (h_top > row_top) ? h_top : row_top;
      int ov_bottom = (h_bottom < row_bottom) ? h_bottom : row_bottom;
      if (ov_top < ov_bottom) {
        graphics_context_set_fill_color(gctx, GColorBlack);
        graphics_fill_rect(gctx, GRect(0, ov_top - row_top, b.size.w, ov_bottom - ov_top), 0, GCornerNone);
      }
    }
    graphics_context_set_text_color(gctx, selected ? GColorWhite : GColorBlack);
    graphics_draw_text(gctx, "+ New timer", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(4, (b.size.h - 26) / 2 - 3, b.size.w - 8, 26),
      GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    return;
  }
  Timer *t = &s_timers[info.timer_idx];
  // Tint each row by state so running/paused/done stand out at a glance (color
  // displays only; b&w falls back to the standard white/black look). The selected
  // row uses a DARK shade of the same hue + white text so it still reads as the
  // cursor AND keeps its state colour; idle selected stays the plain black highlight.
  bool selected = (s_menu_selected_timer_idx == info.timer_idx);
  GColor bg, fg, sel_bg, unused_fg;
  ml_row_colors(t, false, &bg, &unused_fg);
  ml_row_colors(t, true, &sel_bg, &unused_fg);
  ml_row_colors(t, selected, &unused_fg, &fg);
  graphics_context_set_fill_color(gctx, bg);
  graphics_fill_rect(gctx, layer_get_bounds(cell), 0, GCornerNone);
  int hy, hh;
  if (ml_current_highlight_rect(&hy, &hh)) {
    int row_top = ml_row_top_for(ci->row, s_menu_selected_timer_idx);
    int row_bottom = row_top + layer_get_bounds(cell).size.h;
    int h_top = hy;
    int h_bottom = hy + hh;
    int ov_top = (h_top > row_top) ? h_top : row_top;
    int ov_bottom = (h_bottom < row_bottom) ? h_bottom : row_bottom;
    if (ov_top < ov_bottom) {
      graphics_context_set_fill_color(gctx, sel_bg);
      graphics_fill_rect(gctx, GRect(0, ov_top - row_top, layer_get_bounds(cell).size.w, ov_bottom - ov_top), 0, GCornerNone);
    }
  }
  graphics_context_set_text_color(gctx, fg);
  GRect b = layer_get_bounds(cell);
  if (info.kind == ML_ROW_TIMER_DETAIL) {
    bool small = (b.size.w <= 144);
    bool running = (t->state == TS_RUNNING);
    bool stopped = (t->state != TS_RUNNING && t->state != TS_PAUSED);
    GFont f_value = fonts_get_system_font(
      small
        ? (running ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_18)
        : (running ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_24)
    );
    GFont f_suffix = fonts_get_system_font(small ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_24);
    int th = small ? 22 : 28;
    int ty = (b.size.h - th) / 2 - 5;
    char rem[24];
    int32_t detail_secs = stopped ? t->duration : tc_remaining_now(t, now_s());
    tc_format_fixed(rem, sizeof(rem), detail_secs);
    char value_display[40];
    snprintf(value_display, sizeof(value_display), "%s", rem);
    if (!running && launch_sync_applies_for_timer(t)) {
      char sync[16];
      format_launch_sync_suffix(sync, sizeof(sync));
      size_t len = strlen(value_display);
      if (len + 1 < sizeof(value_display)) {
        value_display[len++] = ' ';
        value_display[len] = '\0';
      }
      strncat(value_display, sync, sizeof(value_display) - strlen(value_display) - 1);
    }
    graphics_draw_text(gctx, value_display, f_value, GRect(4, ty, b.size.w - 8, th),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    GSize vw = graphics_text_layout_get_content_size(value_display, f_value,
      GRect(0, 0, b.size.w, th), GTextOverflowModeFill, GTextAlignmentLeft);
    int suffix_x = 4 + vw.w + 4;
    if (suffix_x < b.size.w - 8) {
      graphics_draw_text(gctx, "remaining", f_suffix, GRect(suffix_x, ty, b.size.w - 4 - suffix_x, th),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
    if (ml_is_item_boundary_row(ci->row, s_menu_selected_timer_idx)) {
      graphics_context_set_stroke_color(gctx, GColorDarkGray);
      graphics_draw_line(gctx, GPoint(0, b.size.h - 1), GPoint(b.size.w - 1, b.size.h - 1));
    }
    return;
  }
  // Single line: fixed-width HH:MM:SS time first (bold) so the column aligns and is
  // easy to compare, then the description. State is conveyed by the row tint. On
  // smaller (144px) displays use a smaller font so the description fits.
  bool small = (b.size.w <= 144);
  GFont tf = fonts_get_system_font(small ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_24_BOLD);
  GFont nf = fonts_get_system_font(small ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_24);
  int th = small ? 22 : 28;
  int ty = (b.size.h - th) / 2 - 2;
  int icon_x = 4;
  int icon_y = ty + (th - 12) / 2 + 3;
  ml_draw_state_icon(gctx, icon_x, icon_y, t->state, fg);
  int time_x = icon_x + 16;
  bool show_full_duration = (t->state == TS_RUNNING) || (t->state == TS_PAUSED);
  int32_t primary_secs = show_full_duration ? t->duration : tc_remaining_now(t, now_s());
  char rem[16]; tc_format_fixed(rem, sizeof(rem), primary_secs);
  graphics_draw_text(gctx, rem, tf, GRect(time_x, ty, b.size.w - time_x - 4, th),
    GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  // Start the description just after the time text (the fixed format renders a
  // constant width) with a small gap — much tighter than the old 96px column.
  GSize tw = graphics_text_layout_get_content_size(rem, tf,
    GRect(0, 0, b.size.w, th), GTextOverflowModeFill, GTextAlignmentLeft);
  int desc_x = time_x + tw.w + 4;
  if (t->name[0]) {
    graphics_draw_text(gctx, t->name, nf,
      GRect(desc_x, ty, b.size.w - 4 - desc_x, th),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
  if (ml_is_item_boundary_row(ci->row, s_menu_selected_timer_idx)) {
    graphics_context_set_stroke_color(gctx, GColorDarkGray);
    graphics_draw_line(gctx, GPoint(0, b.size.h - 1), GPoint(b.size.w - 1, b.size.h - 1));
  }
}

static void confirm_timer_cb(void *data) {
  s_confirm_timer = NULL;
  close_to_watchface();   // -> watchface (with exit reason)
}

static void confirm_update_proc(Layer *layer, GContext *gctx) {
  GRect b = layer_get_bounds(layer);
  int cx = b.size.w / 2;
  int cy = b.size.h / 2;
  // white foreground on the saturated-green window (black on the b&w fallback),
  // mirroring the alarm screen's white-on-red look
  GColor fg = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
  // checkmark (two thick strokes)
  graphics_context_set_stroke_color(gctx, fg);
  graphics_context_set_stroke_width(gctx, 6);
  graphics_draw_line(gctx, GPoint(cx - 24, cy - 42), GPoint(cx - 8, cy - 26));
  graphics_draw_line(gctx, GPoint(cx - 8, cy - 26), GPoint(cx + 26, cy - 60));
  graphics_context_set_text_color(gctx, fg);
  // name (or the time, if unnamed) — large
  graphics_draw_text(gctx, s_confirm_name, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
    GRect(2, cy - 18, b.size.w - 4, 36), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  if (s_confirm_named) {
    // duration (only when named, to avoid showing the time twice) — large + bold
    graphics_draw_text(gctx, s_confirm_time, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
      GRect(2, cy + 20, b.size.w - 4, 34), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    graphics_draw_text(gctx, "Started", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(2, cy + 56, b.size.w - 4, 30), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  } else {
    graphics_draw_text(gctx, "Started", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(2, cy + 22, b.size.w - 4, 30), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

static void confirm_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_confirm_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_confirm_layer, confirm_update_proc);
  layer_add_child(root, s_confirm_layer);
}

static void confirm_window_unload(Window *w) {
  if (s_confirm_timer) { app_timer_cancel(s_confirm_timer); s_confirm_timer = NULL; }
  if (s_confirm_layer) { layer_destroy(s_confirm_layer); s_confirm_layer = NULL; }
}

// Flash a "Started" screen for ~1.1s, then pop the whole stack (-> watchface).
// Only called on the idle one-tap start when AutoReturn is on.
static void show_start_confirmation(int idx) {
  Timer *t = &s_timers[idx];
  tc_format_remaining(s_confirm_time, sizeof(s_confirm_time), tc_remaining_now(t, now_s()));
  s_confirm_named = (t->name[0] != 0);
  if (s_confirm_named) {
    strncpy(s_confirm_name, t->name, sizeof(s_confirm_name));
  } else {
    strncpy(s_confirm_name, s_confirm_time, sizeof(s_confirm_name));
  }
  s_confirm_name[sizeof(s_confirm_name) - 1] = 0;
  if (!s_confirm_window) {
    s_confirm_window = window_create();
    window_set_background_color(s_confirm_window, PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorWhite));
    window_set_window_handlers(s_confirm_window,
      (WindowHandlers){ .load = confirm_window_load, .unload = confirm_window_unload });
  }
  window_stack_push(s_confirm_window, true);
  s_confirm_timer = app_timer_register(1100, confirm_timer_cb, NULL);
}

static bool ml_resolve_selected_target(uint16_t row, int *out_idx, bool *out_new_row) {
  MlRowInfo info;
  if (!ml_row_info_for(row, s_menu_selected_timer_idx, &info)) { return false; }
  if (info.kind == ML_ROW_NEW) {
    if (out_new_row) { *out_new_row = true; }
    if (out_idx) { *out_idx = -1; }
    return true;
  }
  if (out_new_row) { *out_new_row = false; }
  if (out_idx) { *out_idx = info.timer_idx; }
  return true;
}

static void ml_select(MenuLayer *ml, MenuIndex *ci, void *ctx) {
  idle_reset();
  int idx = -1;
  bool is_new = false;
  if (!ml_resolve_selected_target(ci->row, &idx, &is_new)) { return; }
  if (is_new) { create_new_timer(); return; }
  // An unstarted (idle) or finished (done) timer has only one useful action —
  // skip the menu, just (re)start it from full duration.
  if (s_timers[idx].state == TS_IDLE || s_timers[idx].state == TS_DONE) {
    int32_t base = s_timers[idx].remaining;
    if (base < 1) { base = s_timers[idx].duration; }
    start_with_secs(&s_timers[idx], launch_adjust_start_secs(base));
    ensure_unnamed_star(idx);
    bool fired = finish_start_tail();
    if (fired) { return; }
    select_timer_row(idx);
    if (s_auto_return) { show_start_confirmation(idx); }   // flash, then pop to watchface
    return;
  } else {
    open_detail_window(idx, DSTYLE_LEGACY);
  }
}

// Long SELECT opens the timer edit menu for any existing row.
static void ml_select_long(MenuLayer *ml, MenuIndex *ci, void *ctx) {
  idle_reset();
  int idx = -1;
  bool is_new = false;
  if (!ml_resolve_selected_target(ci->row, &idx, &is_new) || is_new) { return; }
  open_detail_window(idx, DSTYLE_LONG_EXISTING);
}

static void ml_selection_changed(MenuLayer *ml, MenuIndex new_i, MenuIndex old_i, void *ctx) {
  (void)old_i;
  if (s_menu_internal_selection) { return; }
  MlRowInfo new_info;
  if (!ml_row_info_for(new_i.row, s_menu_selected_timer_idx, &new_info)) { return; }

  int16_t prev_selected = s_menu_selected_timer_idx;

  int16_t next_selected = -1;
  bool moving_down = (new_i.row > old_i.row);
  bool moving_up = (new_i.row < old_i.row);

  if (new_info.kind == ML_ROW_NEW) {
    next_selected = -1;
  } else if (new_info.kind == ML_ROW_TIMER_DETAIL && new_info.timer_idx == prev_selected) {
    // Skip selecting the detail row itself: jump to adjacent logical item.
    int pos = ml_order_pos_for_timer(prev_selected);
    if (moving_down) {
      if (pos >= 0 && pos + 1 < s_count) { next_selected = (int16_t)s_order[pos + 1]; }
      else { next_selected = -1; }
    } else if (moving_up) {
      if (pos > 0) { next_selected = (int16_t)s_order[pos - 1]; }
      else { next_selected = (int16_t)prev_selected; }
    } else {
      next_selected = (int16_t)new_info.timer_idx;
    }
  } else {
    next_selected = (int16_t)new_info.timer_idx;
  }

  s_menu_selected_timer_idx = next_selected;
  int target_row = (next_selected == -1)
    ? ml_row_for_new(next_selected)
    : ml_row_for_timer_primary(next_selected, next_selected);
  bool logical_changed = (next_selected != prev_selected);
  bool cursor_needs_reanchor = (target_row >= 0 && (int)new_i.row != target_row);
  if (!logical_changed && !cursor_needs_reanchor) {
    if (s_empty_hint_layer) { layer_mark_dirty(s_empty_hint_layer); }
    return;
  }
  if (s_menu) {
    s_menu_internal_selection = true;
    int to_y = 0, to_h = 0;
    if (target_row >= 0) { ml_block_for_selection(next_selected, &to_y, &to_h); }
    if (target_row >= 0) {
      menu_layer_set_selected_index(s_menu, (MenuIndex){ .section = 0, .row = (uint16_t)target_row }, MenuRowAlignNone, false);
    }
    s_menu_internal_selection = false;
    if (target_row >= 0 && ml_block_for_selection(next_selected, &to_y, &to_h)) {
      s_menu_visual_y = (int16_t)to_y;
      s_menu_visual_h = (int16_t)to_h;
      s_menu_visual_valid = true;
      menu_layer_reload_data(s_menu);
      ml_scroll_item_bounds_into_view(to_y, to_h);
    } else {
      menu_layer_reload_data(s_menu);
    }
  }
  if (s_empty_hint_layer) { layer_mark_dirty(s_empty_hint_layer); }
}

// ---- AppMessage inbox: a TimerConfig string + SortOrder int -> reconcile ----
static void inbox_received(DictionaryIterator *iter, void *ctx) {
  Tuple *sort = dict_find(iter, MESSAGE_KEY_SortOrder);
  if (sort) {
    int m = (int)sort->value->int32;
    if (m < SORT_MRU || m > SORT_LONGEST) { m = SORT_MRU; }
    s_sort = (SortMode)m;
    store_save_sort(s_sort);
  }
  Tuple *autoret = dict_find(iter, MESSAGE_KEY_AutoReturn);
  if (autoret) {
    s_auto_return = autoret->value->int32 != 0;
    store_save_autoreturn(s_auto_return);
  }
  Tuple *runfirst = dict_find(iter, MESSAGE_KEY_RunningFirst);
  if (runfirst) {
    s_running_first = runfirst->value->int32 != 0;
    store_save_runningfirst(s_running_first);
  }
  Tuple *deffin = dict_find(iter, MESSAGE_KEY_DefaultFinishAction);
  if (deffin) {
    s_default_finish_delete = deffin->value->int32 != 0;
    store_save_default_finish_delete(s_default_finish_delete);
  }
  Tuple *runoncreate = dict_find(iter, MESSAGE_KEY_RunOnCreate);
  if (runoncreate) {
    s_run_on_create = runoncreate->value->int32 != 0;
    store_save_runoncreate(s_run_on_create);
  }
  Tuple *idle = dict_find(iter, MESSAGE_KEY_IdleExitSec);
  int isec = idle_read_seconds(idle);
  if (isec >= 0) {
    s_idle_timeout_sec = isec;
    store_save_idleexit(isec);
    idle_reset();
  }
  Tuple *ls = dict_find(iter, MESSAGE_KEY_LaunchSync);
  if (ls) {
    s_launch_sync = ls->value->int32 != 0;
    store_save_launchsync(s_launch_sync);
    ensure_ticking();
  }
  // Pause the idle auto-exit while the phone config page is open (no watch buttons are
  // pressed during config, so the idle timer would otherwise fire and kill the app —
  // and PKJS with it — closing the config page and losing unsaved changes).
  Tuple *co = dict_find(iter, MESSAGE_KEY_CfgOpen);
  if (co) {
    s_config_open = co->value->int32 ? true : false;
    if (s_config_open) { idle_cancel(); APP_LOG(APP_LOG_LEVEL_INFO, "config open: idle paused"); }
    else               { idle_reset();  APP_LOG(APP_LOG_LEVEL_INFO, "config closed: idle resumed"); }
  }
  Tuple *cfg = dict_find(iter, MESSAGE_KEY_TimerConfig);
  if (cfg && cfg->type == TUPLE_CSTRING) {
    // static, NOT on the stack: two Timer[MAX_TIMERS] arrays are ~2 KB and would
    // overflow the Pebble app stack. The inbox handler runs on the single event
    // loop, so static is safe.
    static Timer cur[MAX_TIMERS];
    static Timer parsed[MAX_TIMERS];
    static Timer merged[MAX_TIMERS];
    static Timer orphans[MAX_TIMERS];
    static bool dof_old[MAX_TIMERS];
    static bool dof_new[MAX_TIMERS];
    static bool draft_old[MAX_TIMERS];
    static bool draft_new[MAX_TIMERS];
    static int8_t st_old[MAX_TIMERS];
    static int8_t st_new[MAX_TIMERS];
    static int8_t orphan_star[MAX_TIMERS];
    int cn = s_count;
    memcpy(cur, s_timers, sizeof(Timer) * (size_t)cn);
    memcpy(dof_old, s_delete_on_finish, sizeof(bool) * (size_t)cn);
    memset(draft_old, 0, sizeof(draft_old));
    if (s_new_timer_idx >= 0 && s_new_timer_idx < cn) { draft_old[s_new_timer_idx] = true; }
    memcpy(st_old, s_unnamed_star, sizeof(int8_t) * (size_t)cn);
    int pn = tc_parse_config(cfg->value->cstring, parsed, MAX_TIMERS);

    // tc_reconcile matches purely by position, so a kept ("After finished: Save")
    // timer that the phone deleted from its config would either be silently
    // dropped or have its live state misapplied to an unrelated, reordered config
    // row. Pull any such timers out (matched by name) before reconciling, and
    // re-append them afterward as watch-local, flipped to "After finished:
    // Delete" - they're no longer represented on the phone.
    int on = 0;
    int compact_n = 0;
    for (int i = 0; i < cn; i++) {
      bool orphaned = false;
      // Note: deliberately not gated on !cur[i].custom - a timer just toggled to
      // "Save" (or a freshly created one) stays custom=true until a later phone
      // Save absorbs it by position, but it can be deleted from the phone before
      // that ever happens, and must still be recognized as orphaned here.
      // Match on (name, duration) rather than name alone, since an unnamed timer
      // ("<No label>") is a normal, common case - matching name-only would either
      // require a non-empty name (missing unnamed orphans entirely) or treat any
      // two unnamed timers as interchangeable.
      if (!dof_old[i]) {
        orphaned = true;
        for (int j = 0; j < pn; j++) {
          if (strcmp(cur[i].name, parsed[j].name) == 0 && cur[i].duration == parsed[j].duration) {
            orphaned = false;
            break;
          }
        }
      }
      if (orphaned && on < MAX_TIMERS) {
        orphans[on] = cur[i];
        orphan_star[on] = st_old[i];
        on++;
      } else {
        cur[compact_n] = cur[i];
        dof_old[compact_n] = dof_old[i];
        draft_old[compact_n] = draft_old[i];
        st_old[compact_n] = st_old[i];
        compact_n++;
      }
    }
    cn = compact_n;

    int mn = tc_reconcile(cur, cn, parsed, pn, merged);
    memcpy(s_timers, merged, sizeof(Timer) * (size_t)mn);
    s_count = mn;
    memset(dof_new, 0, sizeof(dof_new));
    memset(draft_new, 0, sizeof(draft_new));
    for (int i = 0; i < MAX_TIMERS; i++) { st_new[i] = -1; }
    int out = pn;
    for (int i = pn; i < cn && out < mn; i++) {
      if (!cur[i].custom) { continue; }
      dof_new[out++] = dof_old[i];
      draft_new[out - 1] = draft_old[i];
      st_new[out - 1] = st_old[i];
    }
    // Re-append the orphaned (phone-deleted) timers, now watch-local and flipped
    // to "After finished: Delete". If one's already stopped, there's no future
    // finish/stop to catch it - drop it outright instead of re-listing it.
    for (int i = 0; i < on && s_count < MAX_TIMERS; i++) {
      if (orphans[i].state == TS_IDLE || orphans[i].state == TS_DONE) { continue; }
      Timer *t = &s_timers[s_count];
      *t = orphans[i];
      t->custom = true;
      dof_new[s_count] = true;
      st_new[s_count] = orphan_star[i];
      s_count++;
    }
    memcpy(s_delete_on_finish, dof_new, sizeof(dof_new));
    memcpy(s_unnamed_star, st_new, sizeof(st_new));
    for (int i = 0; i < s_count; i++) { ensure_unnamed_star(i); }
    // Rebuild draft-tracking after reconcile index remaps so BACK still discards
    // the unsaved draft timer instead of persisting it.
    s_new_timer_idx = -1;
    for (int i = 0; i < s_count; i++) {
      if (draft_new[i]) { s_new_timer_idx = i; break; }
    }
    sweep_expiries();   // mark stale expiries DONE; no alarm for a config reconcile
    persist_all(); rearm_wakeup(); ensure_ticking();
    // reload_ui() below only refreshes the main list - if the per-timer edit menu
    // is open (e.g. showing "After finished") its label was just built from
    // s_delete_on_finish and won't repaint on its own otherwise.
    if (s_detail_menu && s_detail_window && window_stack_contains_window(s_detail_window)) {
      menu_layer_reload_data(s_detail_menu);
    }
  }
  reload_ui();
}

// Outbox result handlers. These MUST be registered before sending: on hardware
// the phone ACKs our outbound Request and the SDK invokes the result callback —
// if it's NULL the app jumps to a null address and faults (the emulator never
// hits this because there's no phone to ACK).
static void outbox_sent(DictionaryIterator *iter, void *ctx) {}
static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", (int)reason);
}

// Ask the phone for the current config. A watchapp only receives AppMessages
// while running, so config saved on the phone while this app was closed never
// arrived; on launch we request it and the phone replies (see config_sync.ts).
static void request_config(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_uint8(out, MESSAGE_KEY_Request, 1);
    app_message_outbox_send();
  }
}

// Tell the phone to save a new unnamed timer of `secs` seconds (appended to its
// TimerConfig + Clay store). The watch keeps the running timer locally (flagged
// custom) so it survives even if this send fails / the phone is offline.
static void send_add_timer(int32_t secs, const char *name) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_uint32(out, MESSAGE_KEY_AddTimer, (uint32_t)secs);
    if (name) { dict_write_cstring(out, MESSAGE_KEY_AddTimerName, name); }
    app_message_outbox_send();
  }
}

// Tell the phone to drop the timer at list index `idx` from its TimerConfig +
// Clay store. Best-effort, like send_add_timer: if it fails (phone offline) the
// watch still removes it locally, but a later config reconcile will re-add it.
static void send_delete_timer(int32_t idx) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_int32(out, MESSAGE_KEY_DeleteTimer, idx);
    app_message_outbox_send();
  }
}

// Tell the phone to overwrite timer `idx` with a new duration (`secs`), keeping
// its current name. Best-effort, like Add/Delete.
static void send_update_timer(int32_t idx, int32_t secs, const char *name) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_int32(out, MESSAGE_KEY_UpdateTimerIndex, idx);
    dict_write_int32(out, MESSAGE_KEY_UpdateTimerSeconds, secs);
    if (name) { dict_write_cstring(out, MESSAGE_KEY_UpdateTimerName, name); }
    app_message_outbox_send();
  }
}

// Remove the timer at `idx`, shifting the tail down. Caller persists + re-sorts.
static void remove_timer_at(int idx) {
  if (idx < 0 || idx >= s_count) { return; }
  for (int i = idx; i < s_count - 1; i++) {
    s_timers[i] = s_timers[i + 1];
    s_delete_on_finish[i] = s_delete_on_finish[i + 1];
    s_unnamed_star[i] = s_unnamed_star[i + 1];
  }
  s_count--;
  s_delete_on_finish[s_count] = false;
  s_unnamed_star[s_count] = -1;
  // Keep the draft-tracking index valid across the shift: clear it if the draft
  // itself was removed, decrement it if a lower row was removed. Prevents
  // s_new_timer_idx from desyncing (e.g. an alarm snooze re-points s_detail_idx
  // to a fired timer mid-draft; a later delete would otherwise leave
  // s_new_timer_idx pointing at the wrong row).
  if (idx == s_new_timer_idx) { s_new_timer_idx = -1; }
  else if (s_new_timer_idx > idx) { s_new_timer_idx--; }
  if (idx == s_menu_selected_timer_idx) { s_menu_selected_timer_idx = -1; }
  else if (s_menu_selected_timer_idx > idx) { s_menu_selected_timer_idx--; }
}

static void start_as_new(int32_t secs, bool save_to_phone, const char *name) {
  if (s_count >= MAX_TIMERS) {
    return;   // List full: nothing to create. (Keep it simple — no new row.)
  }
  if (secs < 0) { secs = 0; }
  int idx = s_count;
  Timer *t = &s_timers[idx];
  memset(t, 0, sizeof(*t));
  if (name) {
    strncpy(t->name, name, NAME_LEN);
    t->name[NAME_LEN] = '\0';
  } else {
    t->name[0] = 0;
  }
  t->duration = secs;
  t->remaining = secs;
  t->state = TS_IDLE;
  t->custom = true;
  s_delete_on_finish[idx] = !save_to_phone;
  s_unnamed_star[idx] = -1;
  s_count++;
  assign_unnamed_star_for_duration(idx, t->duration);
  bool fired = false;
  if (s_run_on_create) {
    start_with_secs(t, launch_adjust_start_secs_for_timer(t, secs));
    fired = finish_start_tail();
  }
  if (save_to_phone) { send_add_timer(secs, t->name); }
  if (fired) { return; }
  select_timer_row(idx);
  if (s_run_on_create && s_auto_return) { show_start_confirmation(idx); }   // flash -> watchface
  else if (s_detail_window && window_stack_contains_window(s_detail_window)) {
    window_stack_remove(s_detail_window, true);           // back to the list
  }
}

static void apply_overwrite_only(int idx, int32_t secs, const char *name) {
  if (idx < 0 || idx >= s_count) { return; }
  if (secs < 0) { secs = 0; }
  bool was_delete_on_finish = s_delete_on_finish[idx];
  Timer *t = &s_timers[idx];
  if (name) {
    strncpy(t->name, name, NAME_LEN);
    t->name[NAME_LEN] = '\0';
  }
  t->duration = secs;
  if (t->state == TS_IDLE) { t->remaining = secs; }
  if (t->state == TS_DONE) { t->remaining = 0; }
  t->last_used = now_s();
  assign_unnamed_star_for_duration(idx, t->duration);
  s_delete_on_finish[idx] = was_delete_on_finish;
  persist_all(); rearm_wakeup(); reload_ui();
  if (!was_delete_on_finish) { send_update_timer(idx, secs, name); }
  window_stack_remove(s_detail_window, true);
}

// "+ New timer" action: create an unnamed IDLE draft timer (default 1:00) held in
// RAM only (not persisted, not sent to the phone) and open the time dial first.
// The draft is committed by Start/Save actions, or discarded on BACK.
static void create_new_timer(void) {
  if (s_count >= MAX_TIMERS) { return; }   // list full: no-op
  int idx = s_count;
  Timer *t = &s_timers[idx];
  memset(t, 0, sizeof(*t));
  t->name[0] = 0;
  t->duration = 60; t->remaining = 60;     // default 1:00
  t->state = TS_IDLE;
  t->custom = true;                        // survive a mid-draft config reconcile
  t->last_used = now_s();
  s_delete_on_finish[idx] = s_default_finish_delete; // seeded from the "Default action after timer finishes" setting
  s_unnamed_star[idx] = -1;
  s_count++;
  s_new_timer_idx = idx;
  reload_ui();
  open_dial_window(idx, DSTYLE_LONG_NEW);
}

static void empty_hint_update_proc(Layer *layer, GContext *gctx) {
  GRect b = layer_get_bounds(layer);
  if (b.size.h <= 32) { return; }
  const GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24);
  const char *msg = NULL;
  int new_row = ml_row_for_new(s_menu_selected_timer_idx);
  int free_top = 32;
  if (new_row >= 0) {
    free_top = ml_row_top_for((uint16_t)new_row, s_menu_selected_timer_idx) + ML_ROW_H_PRIMARY;
  }
  GRect area = GRect(8, free_top, b.size.w - 16, b.size.h - free_top);
  if (s_count == 0) {
    msg =
      "Configure timers by\n"
      "- \"+ New timer\",\n"
      "- the phone, or\n"
      "- touch";
  } else if (s_count == 1) {
    msg =
      "- Short-press to start or\n"
      "  control running\n"
      "- Long-press to edit";
  } else {
    return;
  }
  if (area.size.h <= 20) { return; }

  GSize sz = graphics_text_layout_get_content_size(
    msg, f, GRect(0, 0, area.size.w, 200), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  int w = sz.w;
  if (w > area.size.w) { w = area.size.w; }
  int x = area.origin.x + (area.size.w - w) / 2;
  // GOTHIC reserves headroom above the caps (see multitap_keyboard.c's font ladder), so a
  // measured content box still sits low when centered; lift it back to the optical middle.
  const int rise = 4;
  int y = area.origin.y + (area.size.h - sz.h) / 2 - rise;
  if (y < area.origin.y) { y = area.origin.y; }
  GRect hint = GRect(x, y, w, sz.h);
  graphics_context_set_text_color(gctx, GColorBlack);
  graphics_draw_text(gctx, msg, f, hint, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

// ---- window ----
static void window_load(Window *w) {
  rebuild_order();
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  GRect menu_bounds = bounds;
  menu_bounds.size.h = bottom_bar_top_for_bounds(bounds);
  s_menu = menu_layer_create(menu_bounds);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = ml_num_rows,
    .get_cell_height = ml_cell_height,
    .draw_row = ml_draw_row,
    .select_click = ml_select,
    .select_long_click = ml_select_long,
    .selection_changed = ml_selection_changed,
  });
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  s_empty_hint_layer = layer_create(menu_bounds);
  layer_set_update_proc(s_empty_hint_layer, empty_hint_update_proc);
  layer_add_child(root, s_empty_hint_layer);
  s_main_bottom_bar_layer = layer_create(bottom_bar_rect_for_bounds(bounds));
  layer_set_update_proc(s_main_bottom_bar_layer, main_bottom_bar_update_proc);
  layer_add_child(root, s_main_bottom_bar_layer);
  s_menu_selected_timer_idx = (s_count > 0) ? s_order[0] : -1;
  s_menu_visual_valid = false;
  s_menu_internal_selection = true;
  menu_layer_set_selected_index(s_menu, (MenuIndex){ .section = 0, .row = 0 }, MenuRowAlignTop, false);
  s_menu_internal_selection = false;
  int y = 0, h = 0;
  if (ml_block_for_selection(s_menu_selected_timer_idx, &y, &h)) {
    s_menu_visual_y = (int16_t)y;
    s_menu_visual_h = (int16_t)h;
    s_menu_visual_valid = true;
  }
}
static void window_unload(Window *w) {
  ml_stop_animation();
  s_menu_visual_valid = false;
  if (s_main_bottom_bar_layer) { layer_destroy(s_main_bottom_bar_layer); s_main_bottom_bar_layer = NULL; }
  if (s_empty_hint_layer) { layer_destroy(s_empty_hint_layer); s_empty_hint_layer = NULL; }
  menu_layer_destroy(s_menu); s_menu = NULL;
}

static void main_appear(Window *w) {
  idle_appear(w);
  dial_touch_create(window_get_root_layer(w), main_touch_selected);
  dial_touch_enable(true);
}

static void main_disappear(Window *w) {
  idle_disappear(w);
  dial_touch_enable(false);
  dial_touch_destroy();
}

static void init(void) {
  s_app_launch_s = now_s();
  s_count = store_load(s_timers);
  memset(s_delete_on_finish, 0, sizeof(s_delete_on_finish));
  for (int i = 0; i < MAX_TIMERS; i++) { s_unnamed_star[i] = -1; }
  uint32_t em = store_load_delete_on_finish_mask();
  for (int i = 0; i < s_count && i < 32; i++) {
    s_delete_on_finish[i] = (em & (1u << i)) != 0;
    ensure_unnamed_star(i);
  }
  s_sort = (SortMode)store_load_sort();
  s_auto_return = store_load_autoreturn();
  s_running_first = store_load_runningfirst();
  s_default_finish_delete = store_load_default_finish_delete();
  s_run_on_create = store_load_runoncreate();
  s_idle_timeout_sec = store_load_idleexit();
  s_launch_sync = store_load_launchsync();
#ifdef SCREENSHOT_FIXTURES
  if (s_count == 0) {
    s_count = 3;
    memset(s_timers, 0, sizeof(s_timers));
    strcpy(s_timers[0].name, "Egg"); s_timers[0].duration = 300; s_timers[0].state = TS_RUNNING; s_timers[0].end_time = time(NULL) + 184; s_timers[0].last_used = time(NULL);
    strcpy(s_timers[1].name, "Tea"); s_timers[1].duration = 120; s_timers[1].state = TS_PAUSED; s_timers[1].remaining = 75; s_timers[1].last_used = time(NULL) - 10;
    strcpy(s_timers[2].name, "Laundry"); s_timers[2].duration = 3600; s_timers[2].state = TS_DONE; s_timers[2].remaining = 0; s_timers[2].last_used = 0;
  }
#endif
  // If launched by a wakeup, the firing event was already consumed; sweep now.
  WakeupId wid; int32_t cookie;
  bool by_wakeup = wakeup_get_launch_event(&wid, &cookie);
  if (by_wakeup) { store_save_wakeup_id(-1); }
  int fired = sweep_expiries();
  if (fired) { persist_all(); }
  rearm_wakeup();

  app_message_register_inbox_received(inbox_received);
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  request_config();   // pull config from the phone (covers app-closed-at-Save case)

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload,
    .appear = main_appear, .disappear = main_disappear });
  window_stack_push(s_window, true);
  rebuild_order();
  ensure_ticking();

  // A timer finished since the app last closed -> show the alarm over the list and
  // buzz. This covers the wakeup-launched case AND a manual open where the wakeup
  // never fired (failed to arm / dropped by the firmware): sweep_expiries only
  // counts a RUNNING timer that has just crossed its end_time, so `fired` is a
  // genuine "you may have missed this finish" signal regardless of launch reason.
  (void)by_wakeup;
  if (fired) { trigger_alarm(s_last_fired_idx, fired); }

#ifdef SCREENSHOT_FIXTURES
  // (list view fixture: 3 seeded timers above show on the list directly)
#endif
}

static void deinit(void) {
  if (s_tick) { app_timer_cancel(s_tick); }
  if (s_new_flow_label_timer) { app_timer_cancel(s_new_flow_label_timer); s_new_flow_label_timer = NULL; }
  idle_cancel();
  if (s_new_timer_idx >= 0 && s_new_timer_idx < s_count) { remove_timer_at(s_new_timer_idx); }
  persist_all();
  rearm_wakeup();   // ensure the closed-app wakeup reflects final state
  if (s_confirm_window) { window_destroy(s_confirm_window); }
  if (s_del_window) { window_destroy(s_del_window); }
  if (s_dial_window) { window_destroy(s_dial_window); }
  if (s_detail_window) { window_destroy(s_detail_window); }
  window_destroy(s_window);
}

int main(void) { init(); app_event_loop(); deinit(); }
