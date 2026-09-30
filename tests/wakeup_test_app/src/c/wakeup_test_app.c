#include <pebble.h>

// Bare-minimum companion app for testing pebble-another-timer's wakeup-
// conflict resolution against a REAL other app's wakeup - not the
// TEMP-TEST-FORCE hack (which only fakes rearm_wakeup() failing inside
// the timer app itself, and never exercises real cross-app exclusion or
// real pre-emption/graceful-close timing). Entirely AppMessage-driven, no
// on-watch interaction at all - a status line is drawn only so a
// `pebble screenshot` can confirm what happened.
//
// Usage (see pebble-emulator skill for --vnc/idle-exit details):
//   pebble install --emulator emery --vnc
//   pebble send-app-message --emulator emery --vnc
//     --app-uuid 89a89b3c-f84e-4233-bb93-82a9a247aab8
//     --int ScheduleWakeup=<seconds-from-now>
//   pebble send-app-message --emulator emery --vnc
//     --app-uuid 89a89b3c-f84e-4233-bb93-82a9a247aab8
//     --int CancelWakeup=0
//
// ScheduleWakeup replaces any wakeup this app already has pending (only
// one at a time). After scheduling, the app pops itself to the watchface
// a couple seconds later - it has to actually leave the foreground for
// its own wakeup to ever be able to pre-empt whatever's running by then
// (that's the whole point of this tool). When the wakeup fires, this app
// briefly vibrates and shows the fire time, then exits again the same way.

static Window *s_window;
static TextLayer *s_status_layer;
static char s_status_buf[64];
static AppTimer *s_exit_timer;

static int32_t s_wakeup_id = -1;   // -1 = none pending; mirrors PERSIST_KEY_WAKEUP_ID below
#define PERSIST_KEY_WAKEUP_ID 1

static void set_status(const char *text) {
  snprintf(s_status_buf, sizeof(s_status_buf), "%s", text);
  if (s_status_layer) { text_layer_set_text(s_status_layer, s_status_buf); }
}

static void format_hms(char *buf, size_t n, time_t t) {
  struct tm *lt = localtime(&t);
  strftime(buf, n, "%H:%M:%S", lt);
}

static void exit_to_watchface_cb(void *ctx) {
  s_exit_timer = NULL;
  exit_reason_set(APP_EXIT_ACTION_PERFORMED_SUCCESSFULLY);
  window_stack_pop_all(true);
}

// Leaves the status line up long enough to be caught by a screenshot taken
// right after the triggering AppMessage/wakeup, then gets out of the way.
static void schedule_exit(void) {
  if (s_exit_timer) { app_timer_cancel(s_exit_timer); }
  s_exit_timer = app_timer_register(2000, exit_to_watchface_cb, NULL);
}

static void do_schedule(int32_t offset_s) {
  if (s_wakeup_id >= 0) {
    wakeup_cancel(s_wakeup_id);
    s_wakeup_id = -1;
    persist_write_int(PERSIST_KEY_WAKEUP_ID, -1);
  }
  time_t target = time(NULL) + offset_s;
  WakeupId id = wakeup_schedule(target, 0, true);
  if (id < 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Schedule failed (%d)", (int)id);
    set_status(buf);
    schedule_exit();
    return;
  }
  s_wakeup_id = id;
  persist_write_int(PERSIST_KEY_WAKEUP_ID, id);
  char hms[16];
  format_hms(hms, sizeof(hms), target);
  char buf[64];
  snprintf(buf, sizeof(buf), "Armed for %s\n(+%ds)", hms, (int)offset_s);
  set_status(buf);
  APP_LOG(APP_LOG_LEVEL_INFO, "wakeup_test_app: armed id=%d for %s (+%ds)", (int)id, hms, (int)offset_s);
  schedule_exit();
}

static void do_cancel(void) {
  if (s_wakeup_id >= 0) {
    wakeup_cancel(s_wakeup_id);
    s_wakeup_id = -1;
    persist_write_int(PERSIST_KEY_WAKEUP_ID, -1);
    set_status("Cancelled");
  } else {
    set_status("Nothing to cancel");
  }
  schedule_exit();
}

static void inbox_received(DictionaryIterator *iter, void *ctx) {
  Tuple *t = dict_read_first(iter);
  while (t) {
    if (t->key == MESSAGE_KEY_ScheduleWakeup) {
      do_schedule(t->value->int32);
    } else if (t->key == MESSAGE_KEY_CancelWakeup) {
      do_cancel();
    }
    t = dict_read_next(iter);
  }
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_status_layer = text_layer_create(GRect(0, 0, b.size.w, b.size.h));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text(s_status_layer, s_status_buf);
  layer_add_child(root, text_layer_get_layer(s_status_layer));
}

static void window_unload(Window *w) {
  text_layer_destroy(s_status_layer);
  s_status_layer = NULL;
}

static void init(void) {
  s_wakeup_id = persist_exists(PERSIST_KEY_WAKEUP_ID) ? persist_read_int(PERSIST_KEY_WAKEUP_ID) : -1;

  WakeupId wid; int32_t cookie;
  bool by_wakeup = wakeup_get_launch_event(&wid, &cookie);
  if (by_wakeup) {
    s_wakeup_id = -1;
    persist_write_int(PERSIST_KEY_WAKEUP_ID, -1);
    vibes_short_pulse();
    char hms[16];
    format_hms(hms, sizeof(hms), time(NULL));
    snprintf(s_status_buf, sizeof(s_status_buf), "Fired at %s", hms);
    APP_LOG(APP_LOG_LEVEL_INFO, "wakeup_test_app: fired at %s", hms);
  } else if (s_wakeup_id >= 0) {
    // A manual reopen while a wakeup from an earlier session is still
    // pending (not yet fired) - distinguishes "still armed" from "already
    // fired and cleared" without needing to send another AppMessage first.
    snprintf(s_status_buf, sizeof(s_status_buf), "Still armed\n(id %d)", (int)s_wakeup_id);
  } else {
    snprintf(s_status_buf, sizeof(s_status_buf), "Idle");
  }

  app_message_register_inbox_received(inbox_received);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);

  if (by_wakeup) { schedule_exit(); }
}

static void deinit(void) {
  if (s_exit_timer) { app_timer_cancel(s_exit_timer); }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
