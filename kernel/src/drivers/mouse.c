#include "../include/drivers/mouse.h"

static volatile mouse_state_t g_mouse;
static volatile int32_t       g_screen_w = 1024;
static volatile int32_t       g_screen_h = 768;
static int                    g_inited   = 0;

static void mouse_ensure_init(void) {
    if (g_inited) return;
    g_mouse.x = g_screen_w / 2;
    g_mouse.y = g_screen_h / 2;
    g_mouse.btn_left = g_mouse.btn_right = g_mouse.btn_middle = false;
    g_mouse.scroll = MOUSE_SCROLL_NONE;
    g_inited = 1;
}

void mouse_set_screen(int32_t w, int32_t h) {
    if (w > 0) g_screen_w = w;
    if (h > 0) g_screen_h = h;
    mouse_ensure_init();
    g_mouse.x = g_screen_w / 2;
    g_mouse.y = g_screen_h / 2;
}

static void mouse_clamp(void) {
    if (g_mouse.x < 0)            g_mouse.x = 0;
    if (g_mouse.y < 0)            g_mouse.y = 0;
    if (g_mouse.x >= g_screen_w)  g_mouse.x = g_screen_w - 1;
    if (g_mouse.y >= g_screen_h)  g_mouse.y = g_screen_h - 1;
}

static mouse_scroll_t wheel_to_scroll(int32_t wheel) {
    if (wheel > 0) return MOUSE_SCROLL_UP;
    if (wheel < 0) return MOUSE_SCROLL_DOWN;
    return MOUSE_SCROLL_NONE;
}

void mouse_inject_rel(int32_t dx, int32_t dy,
                      bool btn_left, bool btn_right, bool btn_middle,
                      int32_t wheel) {
    mouse_ensure_init();
    g_mouse.x += dx;
    g_mouse.y += dy;
    mouse_clamp();
    g_mouse.btn_left   = btn_left;
    g_mouse.btn_right  = btn_right;
    g_mouse.btn_middle = btn_middle;
    g_mouse.scroll     = wheel_to_scroll(wheel);
}

void mouse_inject_abs(int32_t x, int32_t y,
                      bool btn_left, bool btn_right, bool btn_middle,
                      int32_t wheel) {
    mouse_ensure_init();
    g_mouse.x = x;
    g_mouse.y = y;
    mouse_clamp();
    g_mouse.btn_left   = btn_left;
    g_mouse.btn_right  = btn_right;
    g_mouse.btn_middle = btn_middle;
    g_mouse.scroll     = wheel_to_scroll(wheel);
}

const mouse_state_t *mouse_get_state(void) {
    mouse_ensure_init();
    return (const mouse_state_t *)&g_mouse;
}
