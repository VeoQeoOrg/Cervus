#include "../../include/console/console.h"
#include "../../include/graphics/fb/fb.h"
#include "../../include/memory/pmm.h"
#include "../../include/sched/spinlock.h"
#include <limine.h>

extern struct limine_framebuffer *global_framebuffer;
extern void kb_buf_push(char c);
extern int  putchar(int);
extern void putchar_flush_begin(void);
extern void putchar_flush_end(void);
extern void draw_cursor(void);
extern void erase_cursor(void);
extern uint32_t get_cursor_row(void);
extern uint32_t get_cursor_col(void);

static spinlock_t g_lock = SPINLOCK_INIT;

typedef struct {
    int             in_use;
    int             is_monitor;
    int             has_shell;
    int             needs_shell;
    vt_cell_t      *grid;
    console_state_t state;
} vt_slot_t;

static vt_slot_t g_vts[VT_COUNT];
static int g_active;
static int g_inited;
static uint32_t g_cols, g_rows;

int vt_active(void) { return g_active; }

static vt_cell_t *grid_alloc(void) {
    return (vt_cell_t *)kzalloc((size_t)g_cols * g_rows * sizeof(vt_cell_t));
}

void vt_init(void) {
    if (g_inited) return;
    g_cols = global_framebuffer ? global_framebuffer->width  / 8  : 80;
    g_rows = global_framebuffer ? global_framebuffer->height / 16 : 25;
    for (int i = 0; i < VT_COUNT; i++) {
        g_vts[i].in_use      = 0;
        g_vts[i].is_monitor  = (i == VT_MONITOR_INDEX);
        g_vts[i].has_shell   = 0;
        g_vts[i].needs_shell = 0;
        g_vts[i].grid        = NULL;
    }
    g_vts[0].grid      = grid_alloc();
    g_vts[0].in_use    = 1;
    g_vts[0].has_shell = 1;
    console_set_grid(g_vts[0].grid, g_cols, g_rows);
    console_save_state(&g_vts[0].state);
    g_active = 0;
    g_inited = 1;
    monitor_init();
}

static int ensure_grid(int n) {
    if (g_vts[n].grid) return 0;
    vt_cell_t *g = grid_alloc();
    if (!g) return -1;
    g_vts[n].grid = g;
    return 1;
}

void vt_switch(int n) {
    if (!g_inited) return;
    if (n < 0 || n >= VT_COUNT) return;
    if (n == g_active) return;

    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    if (n == g_active) { spinlock_release_irqrestore(&g_lock, f); return; }

    if (g_active != VT_MONITOR_INDEX)
        console_save_state(&g_vts[g_active].state);

    if (g_vts[n].is_monitor) {
        console_set_grid(NULL, 0, 0);
        g_active = n;
        monitor_activate();
        spinlock_release_irqrestore(&g_lock, f);
        return;
    }

    if (ensure_grid(n) < 0) { spinlock_release_irqrestore(&g_lock, f); return; }

    console_set_grid(g_vts[n].grid, g_cols, g_rows);
    if (!g_vts[n].in_use) {
        console_reset_state();
        console_save_state(&g_vts[n].state);
        g_vts[n].in_use = 1;
    }
    if (!g_vts[n].has_shell)
        g_vts[n].needs_shell = 1;

    console_load_state(&g_vts[n].state);
    g_active = n;
    console_redraw_grid();
    fb_flush(global_framebuffer);
    spinlock_release_irqrestore(&g_lock, f);
}

void vt_write(int n, const char *buf, size_t len) {
    if (!g_inited || !buf || len == 0) return;
    if (n < 0 || n >= VT_COUNT || g_vts[n].is_monitor) return;

    uint64_t f = spinlock_acquire_irqsave(&g_lock);

    if (ensure_grid(n) < 0) { spinlock_release_irqrestore(&g_lock, f); return; }

    if (n == g_active) {
        putchar_flush_begin();
        for (size_t i = 0; i < len; i++) putchar((int)(unsigned char)buf[i]);
        putchar_flush_end();
        spinlock_release_irqrestore(&g_lock, f);
        return;
    }

    console_state_t saved;
    console_save_state(&saved);

    console_set_grid(g_vts[n].grid, g_cols, g_rows);
    if (!g_vts[n].in_use) {
        console_reset_state();
        g_vts[n].in_use = 1;
    } else {
        console_load_state(&g_vts[n].state);
    }

    console_set_offscreen(1);
    for (size_t i = 0; i < len; i++) putchar((int)(unsigned char)buf[i]);
    console_set_offscreen(0);

    console_save_state(&g_vts[n].state);
    console_set_grid(g_vts[g_active].grid, g_cols, g_rows);
    console_load_state(&saved);

    spinlock_release_irqrestore(&g_lock, f);
}

void vt_handle_chord(int fn) {
    if (fn < 1 || fn > VT_COUNT) return;
    vt_switch(fn - 1);
}

void vt_tick_flush(void) {
    if (!g_inited) return;
    if (!spinlock_try_acquire(&g_lock)) return;
    console_flush_pending();
    spinlock_release(&g_lock);
}

void vt_cursor(int vt, int on) {
    if (!g_inited || vt < 0 || vt >= VT_COUNT) return;
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    if (vt == g_active && !g_vts[vt].is_monitor) {
        if (on) draw_cursor();
        else    erase_cursor();
    }
    spinlock_release_irqrestore(&g_lock, f);
}

void vt_get_cursor(int vt, uint32_t *row, uint32_t *col) {
    if (row) *row = 0;
    if (col) *col = 0;
    if (!g_inited || vt < 0 || vt >= VT_COUNT) return;
    if (g_vts[vt].is_monitor) return;
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    if (vt == g_active) {
        if (row) *row = get_cursor_row();
        if (col) *col = get_cursor_col();
    } else {
        if (row) *row = g_vts[vt].state.cursor_y / 16;
        if (col) *col = g_vts[vt].state.cursor_x / 8;
    }
    spinlock_release_irqrestore(&g_lock, f);
}

void console_input_char(char c) {
    if (g_inited && g_active == VT_MONITOR_INDEX) {
        monitor_input(c);
        return;
    }
    tty_vt_input(g_active, c);
}

int vt_take_spawn_request(void) {
    for (int i = 0; i < VT_COUNT; i++) {
        if (g_vts[i].needs_shell && !g_vts[i].has_shell) {
            g_vts[i].needs_shell = 0;
            g_vts[i].has_shell = 1;
            return i;
        }
    }
    return -1;
}

void vt_mark_shell_running(int n, int running) {
    if (n < 0 || n >= VT_COUNT) return;
    g_vts[n].has_shell = running ? 1 : 0;
    if (!running) g_vts[n].needs_shell = 0;
}
