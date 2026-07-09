#include "ui_touch.h"

#define UI_TOUCH_MAX 4

typedef struct { UiTouchFn fn; void *ctx; } Client;
static Client s_clients[UI_TOUCH_MAX];
static int    s_count;

static void dispatch(const TouchEvent *e, void *context) {
  (void)context;
  // Snapshot first: a client may unregister (even destroy itself) mid-event.
  Client c[UI_TOUCH_MAX];
  int n = s_count;
  memcpy(c, s_clients, sizeof c);
  for (int i = 0; i < n; i++) c[i].fn(e, c[i].ctx);
}

bool ui_touch_register(UiTouchFn fn, void *ctx) {
  if (!fn || !touch_service_is_enabled()) return false;
  for (int i = 0; i < s_count; i++)
    if (s_clients[i].fn == fn && s_clients[i].ctx == ctx) return true;
  if (s_count >= UI_TOUCH_MAX) return false;
  s_clients[s_count++] = (Client){ fn, ctx };
  if (s_count == 1) touch_service_subscribe(dispatch, NULL);
  return true;
}

void ui_touch_unregister(UiTouchFn fn, void *ctx) {
  for (int i = 0; i < s_count; i++) {
    if (s_clients[i].fn != fn || s_clients[i].ctx != ctx) continue;
    for (int j = i; j + 1 < s_count; j++) s_clients[j] = s_clients[j + 1];
    s_count--;
    if (s_count == 0) touch_service_unsubscribe();
    return;
  }
}
