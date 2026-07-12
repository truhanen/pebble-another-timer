#include <pebble.h>
#include "multitap_keyboard_window.h"
#include "multitap_keyboard.h"
#include "keyboard_settings.h"

// Full-screen modal keyboard wrapper.

#define HOLD_MS 350   // touch-down longer than this fires a hold, not a tap

static Window     *s_window;
static MultitapKeyboard *s_keyboard;

static MultitapKeyboardResultHandler s_handler;
static void       *s_context;
static char        s_initial[256];   // copied starting text
static bool        s_has_initial;
static int         s_max_len;        // byte cap on entry (0 = unlimited)
static bool        s_done;            // guard: deliver the result exactly once

// Tap vs. hold disambiguation, identical to the verified PebbleOS touch flow.
static GPoint    s_touch_pt;
static AppTimer *s_hold_timer;
static bool      s_held;
static int       s_touch_key;          // cell the press started on (-1 = off grid)
static bool      s_canceled;           // finger slid off that cell: no tap/hold
static AppTimer *s_del_repeat_timer;   // hold-to-erase on the on-screen DEL key
static bool      s_touch_registered;   // whether we actually subscribed to touch

// ---- Touch -----------------------------------------------------------------

static void prv_del_repeat_cancel(void) {
  if (s_del_repeat_timer) { app_timer_cancel(s_del_repeat_timer); s_del_repeat_timer = NULL; }
}

// Keep erasing while DEL is held, at the same cadence as the Down button, until
// liftoff cancels the timer.
static void prv_del_repeat(void *data) {
  s_del_repeat_timer = NULL;
  multitap_keyboard_backspace(s_keyboard);
  s_del_repeat_timer = app_timer_register(
      multitap_keyboard_delete_repeat_ms(s_keyboard), prv_del_repeat, NULL);
}

static void prv_hold_fire(void *data) {
  s_hold_timer = NULL;
  s_held = true;
  // DEL holds repeat (erase continuously), so it stays visually pressed until the
  // finger lifts. Every other key gets a one-shot hold, so its pressed look drops
  // the moment the hold fires.
  if (multitap_keyboard_point_is_delete(s_keyboard, s_touch_pt)) {
    multitap_keyboard_backspace(s_keyboard);   // first erase now...
    s_del_repeat_timer = app_timer_register(
        multitap_keyboard_delete_repeat_ms(s_keyboard), prv_del_repeat, NULL);  // ...then keep going
    return;
  }
  multitap_keyboard_handle_touch_up(s_keyboard);   // one-shot hold fired: drop the pressed look
  multitap_keyboard_handle_hold(s_keyboard, s_touch_pt);
}

static void prv_touch_handler(const TouchEvent *event, void *context) {
  // The registration lives for the whole keyboard session, but the settings /
  // character / help windows stack on top of it — taps meant for them must not
  // type into the obscured keyboard. Act only while the keyboard is visible.
  if (window_stack_get_top_window() != s_window) return;
  switch (event->type) {
    case TouchEvent_Touchdown:
      s_touch_pt = GPoint(event->x, event->y);
      s_held = false;
      s_canceled = false;
      s_touch_key = multitap_keyboard_key_at_point(s_keyboard, s_touch_pt);
      if (s_hold_timer) app_timer_cancel(s_hold_timer);
      prv_del_repeat_cancel();
      s_hold_timer = app_timer_register(HOLD_MS, prv_hold_fire, NULL);
      multitap_keyboard_handle_touch_down(s_keyboard, s_touch_pt);   // pressed look now
      break;
    case TouchEvent_PositionUpdate: {
      if (s_held) break;   // long-press already fired; the gesture is spent
      // Slide off the starting key -> cancel: kill the pending long-press, drop the
      // pressed look, and skip the tap on release. Sliding back on re-arms the tap.
      int key = multitap_keyboard_key_at_point(s_keyboard, GPoint(event->x, event->y));
      if (key != s_touch_key && !s_canceled) {
        s_canceled = true;
        if (s_hold_timer) { app_timer_cancel(s_hold_timer); s_hold_timer = NULL; }
        multitap_keyboard_handle_touch_up(s_keyboard);
      } else if (key == s_touch_key && s_canceled) {
        s_canceled = false;
        multitap_keyboard_handle_touch_down(s_keyboard, s_touch_pt);
      }
      break;
    }
    case TouchEvent_Liftoff:
      if (s_hold_timer) { app_timer_cancel(s_hold_timer); s_hold_timer = NULL; }
      prv_del_repeat_cancel();
      multitap_keyboard_handle_touch_up(s_keyboard);                 // release the look
      // The keyboard fills the window, so screen coords == layer-local coords.
      if (!s_held && !s_canceled) multitap_keyboard_handle_tap(s_keyboard, s_touch_pt);
      break;
  }
}

// ---- Result delivery -------------------------------------------------------

// Hand the result back to the app and close. `text` is NULL on cancel. The
// keyboard's submit fires this as its last action, so removing the window
// (which destroys the keyboard on unload) is safe here. Remove our own window
// rather than popping the top: a handler is allowed to push a new window, and
// window_stack_pop(true) would tear that one down instead.
static void prv_finish(const char *text) {
  if (s_done) return;
  s_done = true;
  if (s_handler) s_handler(text, s_context);   // text valid only during call
  window_stack_remove(s_window, true);
}

static void prv_kb_done(const char *text, void *context) { prv_finish(text); }

// ---- Buttons ---------------------------------------------------------------

static void prv_up_click(ClickRecognizerRef rec, void *ctx) {
  settings_window_push(s_keyboard);        // Up opens the settings menu
}
static void prv_select_click(ClickRecognizerRef rec, void *ctx) {
  multitap_keyboard_submit(s_keyboard);          // -> prv_kb_done -> prv_finish
}
static void prv_select_long(ClickRecognizerRef rec, void *ctx) {
  multitap_keyboard_newline(s_keyboard);
}
static void prv_down_click(ClickRecognizerRef rec, void *ctx) {
  multitap_keyboard_cycle_page(s_keyboard);      // Down cycles the keyboard type (layout)
}
// Down held = backspace, with the same hold-to-repeat cadence as the on-screen
// DEL key. The down handler erases once and arms the repeat; the up handler
// (button released) stops it.
static void prv_down_long_down(ClickRecognizerRef rec, void *ctx) {
  prv_del_repeat_cancel();
  multitap_keyboard_backspace(s_keyboard);
  s_del_repeat_timer = app_timer_register(
      multitap_keyboard_delete_repeat_ms(s_keyboard), prv_del_repeat, NULL);
}
static void prv_down_long_up(ClickRecognizerRef rec, void *ctx) {
  prv_del_repeat_cancel();
}
static void prv_back_click(ClickRecognizerRef rec, void *ctx) {
  prv_finish(NULL);                        // cancel
}

static void prv_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, prv_select_long, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click);
  window_long_click_subscribe(BUTTON_ID_DOWN, 0, prv_down_long_down, prv_down_long_up);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_click);  // override exit
}

// ---- Lifecycle -------------------------------------------------------------

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_keyboard = multitap_keyboard_create(bounds, prv_kb_done, NULL);
  layer_add_child(root, multitap_keyboard_get_layer(s_keyboard));
  settings_load_and_apply(s_keyboard);
  if (s_max_len > 0) multitap_keyboard_set_max_len(s_keyboard, s_max_len);
  if (s_has_initial) multitap_keyboard_set_text(s_keyboard, s_initial);

  s_touch_registered = touch_service_is_enabled();
  if (s_touch_registered) touch_service_subscribe(prv_touch_handler, NULL);
  else APP_LOG(APP_LOG_LEVEL_WARNING, "Touch not available on this watch");
}

static void prv_unload(Window *window) {
  if (s_touch_registered) touch_service_unsubscribe();
  if (s_hold_timer) { app_timer_cancel(s_hold_timer); s_hold_timer = NULL; }
  prv_del_repeat_cancel();
  multitap_keyboard_destroy(s_keyboard);
  s_keyboard = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void multitap_keyboard_window_push(MultitapKeyboardResultHandler handler,
                             const char *initial_text, void *context) {
  multitap_keyboard_window_push_ex(handler, initial_text, 0, context);
}

void multitap_keyboard_window_push_ex(MultitapKeyboardResultHandler handler,
                                const char *initial_text, int max_len,
                                void *context) {
  if (s_window) return;                    // single instance

  s_handler = handler;
  s_context = context;
  s_done = false;
  s_max_len = max_len;
  s_has_initial = (initial_text != NULL);
  if (s_has_initial) {
    strncpy(s_initial, initial_text, sizeof(s_initial) - 1);
    s_initial[sizeof(s_initial) - 1] = '\0';
  }

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, prv_click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_load,
    .unload = prv_unload,
  });
  window_stack_push(s_window, true);
}
