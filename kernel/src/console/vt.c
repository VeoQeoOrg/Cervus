#include "../../include/console/console.h"
#include "../../include/graphics/fb/fb.h"
#include "../../include/syscall/errno.h"
#include "../../include/memory/pmm.h"
#include "../../include/sched/spinlock.h"
#include "../../include/sched/sched.h"
#include "../../include/memory/vmm.h"
#include "../../include/apic/apic.h"
#include "../../include/smp/smp.h"
#include <stdlib.h>
#include <string.h>

extern fb_info_t *global_framebuffer;
extern void kb_buf_push(char c);
extern int  putchar(int);
extern void putchar_flush_begin(void);
extern void putchar_flush_end(void);
extern void draw_cursor(void);
extern void erase_cursor(void);
extern int  console_cursor_visible(void);
extern int  console_take_reply(char *out, int cap);
extern void tty_vt_input(int vt, char c);
extern uint64_t sched_now_ns(void);
extern uint32_t get_cursor_row(void);
extern uint32_t get_cursor_col(void);

static spinlock_t g_lock = SPINLOCK_INIT;

#define BLINK_PERIOD_NS 500000000ULL

static uint64_t g_blink_next;
static int      g_blink_on = 1;

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
    size_t n = (size_t)g_cols * g_rows;
    vt_cell_t *g = (vt_cell_t *)kzalloc(n * sizeof(vt_cell_t));
    if (!g) return NULL;
    uint32_t fg = console_theme_fg(), bg = console_theme_bg();
    for (size_t i = 0; i < n; i++) { g[i].ch = ' '; g[i].fg = fg; g[i].bg = bg; }
    return g;
}

void vt_init(void) {
    if (g_inited) return;
    g_cols = global_framebuffer ? global_framebuffer->width  / fb_font_width()  : 80;
    g_rows = global_framebuffer ? global_framebuffer->height / fb_font_height() : 25;
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

static void   *g_fb_task[VT_COUNT];
static uint8_t g_fb_drm[VT_COUNT];
static uint8_t g_kbd_off[VT_COUNT];

static int vt_valid(int vt) { return vt >= 0 && vt < VT_COUNT; }

typedef struct {
    void      *task;
    uintptr_t  uaddr;
    uint64_t   pages;
    uintptr_t  vram_phys;
    uintptr_t *shadow;
    uint64_t   nshadow;
} fb_park_t;

static fb_park_t g_park[VT_COUNT];

#define PARK_VRAM_FLAGS (VMM_PRESENT | VMM_USER | VMM_WRITE | VMM_NOEXEC | VMM_SHARED | VMM_PWT | VMM_PAT)
#define PARK_RAM_FLAGS  (VMM_PRESENT | VMM_USER | VMM_WRITE | VMM_NOEXEC | VMM_SHARED)

static void park_flush_tlb(void) {
    if (smp_get_cpu_count() > 1) ipi_tlb_shootdown_broadcast(NULL, TLB_FLUSH_ALL);
}

static vmm_pagemap_t *park_pagemap(fb_park_t *p) {
    task_t *t = p->task;
    return t ? t->pagemap : NULL;
}

static uint64_t park_fb_bytes(void) {
    fb_info_t *fb = global_framebuffer;
    return fb ? (uint64_t)fb->pitch * fb->height : 0;
}

static void park_free_shadow(fb_park_t *p) {
    if (!p->shadow) return;
    for (uint64_t i = 0; i < p->nshadow; i++)
        if (p->shadow[i]) pmm_free(pmm_phys_to_virt(p->shadow[i]), 1);
    free(p->shadow);
    p->shadow = NULL;
    p->nshadow = 0;
}

static void fb_park(int vt) {
    fb_park_t *p = &g_park[vt];
    uint64_t bytes = park_fb_bytes();
    if (p->shadow || !bytes) return;
    uint64_t n = (bytes + 0xFFF) >> 12;
    uintptr_t *list = calloc(n, sizeof(uintptr_t));
    if (!list) return;
    const uint8_t *vram = (const uint8_t *)global_framebuffer->address;
    for (uint64_t i = 0; i < n; i++) {
        void *pg = pmm_alloc(1);
        if (!pg) {
            p->shadow = list;
            p->nshadow = i;
            park_free_shadow(p);
            return;
        }
        uint64_t off = i << 12;
        uint64_t len = bytes - off < 0x1000 ? bytes - off : 0x1000;
        memcpy(pg, vram + off, (size_t)len);
        list[i] = pmm_virt_to_phys(pg);
    }
    p->shadow = list;
    p->nshadow = n;

    vmm_pagemap_t *pm = park_pagemap(p);
    if (pm && p->pages) {
        for (uint64_t i = 0; i < p->pages && i < n; i++)
            vmm_map_page(pm, p->uaddr + (i << 12), list[i], PARK_RAM_FLAGS);
        park_flush_tlb();
    }
}

static void fb_unpark(int vt) {
    fb_park_t *p = &g_park[vt];
    uint64_t bytes = park_fb_bytes();
    if (!p->shadow || !bytes) return;
    uint8_t *vram = (uint8_t *)global_framebuffer->address;
    for (uint64_t i = 0; i < p->nshadow; i++) {
        uint64_t off = i << 12;
        if (off >= bytes) break;
        uint64_t len = bytes - off < 0x1000 ? bytes - off : 0x1000;
        memcpy(vram + off, pmm_phys_to_virt(p->shadow[i]), (size_t)len);
    }
    vmm_pagemap_t *pm = park_pagemap(p);
    if (pm && p->pages) {
        for (uint64_t i = 0; i < p->pages; i++)
            vmm_map_page(pm, p->uaddr + (i << 12), p->vram_phys + (i << 12), PARK_VRAM_FLAGS);
        park_flush_tlb();
    }
    park_free_shadow(p);
}

static void fb_park_drop(int vt) {
    fb_park_t *p = &g_park[vt];
    if (p->shadow) {
        vmm_pagemap_t *pm = park_pagemap(p);
        if (pm && p->pages) {
            for (uint64_t i = 0; i < p->pages; i++)
                vmm_unmap_page_noflush(pm, p->uaddr + (i << 12));
            park_flush_tlb();
        }
        park_free_shadow(p);
    }
    p->task = NULL;
    p->uaddr = 0;
    p->pages = 0;
}

void vt_fb_mapped(int vt, void *task, uintptr_t uaddr, uint64_t pages, uintptr_t vram_phys) {
    if (!vt_valid(vt) || !task) return;
    g_park[vt].task = task;
    g_park[vt].uaddr = uaddr;
    g_park[vt].pages = pages;
    g_park[vt].vram_phys = vram_phys;
}

void vt_kbd_off(int vt) { if (vt_valid(vt)) g_kbd_off[vt] = 1; }

int vt_kbd_muted(void) {
    return g_inited && vt_valid(g_active) && g_kbd_off[g_active];
}

int vt_fb_owned(int vt) { return vt_valid(vt) && g_fb_task[vt] != NULL; }

int vt_fb_owner_is(int vt, void *task) { return vt_valid(vt) && task && g_fb_task[vt] == task; }

int vt_fb_claim(int vt, void *task, int drm) {
    if (!vt_valid(vt) || !task) return -EINVAL;
    if (g_fb_task[vt] && g_fb_task[vt] != task) return -EBUSY;
    g_fb_task[vt] = task;
    g_fb_drm[vt]  = (uint8_t)(drm != 0);
    if (vt == g_active) console_set_offscreen(1);
    return 0;
}

void vt_fb_unclaim(int vt, void *task) {
    if (!vt_valid(vt) || !task || g_fb_task[vt] != task) return;
    fb_park_drop(vt);
    g_fb_task[vt] = NULL;
    g_fb_drm[vt]  = 0;
    g_kbd_off[vt] = 0;
    if (vt == g_active) {
        extern void console_force_full_redraw(void);
        console_set_offscreen(0);
        console_force_full_redraw();
    }
}

int vt_fb_may_draw(int vt, void *task) {
    return vt_valid(vt) && vt == g_active && (!g_fb_task[vt] || g_fb_task[vt] == task);
}

void vt_fb_task_exit(void *task) {
    if (!task) return;
    for (int vt = 0; vt < VT_COUNT; vt++) {
        if (g_fb_task[vt] != task) continue;
        if (g_fb_drm[vt]) {
            extern void drm_forget_scanout(void);
            drm_forget_scanout();
        }
        vt_fb_unclaim(vt, task);
    }
}

void vt_switch(int n) {
    if (!g_inited) return;
    if (n < 0 || n >= VT_COUNT) return;
    if (n == g_active) return;

    int old = g_active;
    if (vt_valid(old) && g_fb_task[old] && !g_fb_drm[old]) fb_park(old);

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

    int owns = g_fb_task[n] != NULL;
    console_set_offscreen(owns);
    if (owns) {
        fb_clear(global_framebuffer, 0);
        fb_flush(global_framebuffer);
    } else {
        console_redraw_grid();
        fb_flush(global_framebuffer);
    }
    spinlock_release_irqrestore(&g_lock, f);

    extern void seat_vt_switched(int vt);
    extern void input_release_held(void);
    input_release_held();
    seat_vt_switched(n);

    if (owns) {
        extern void drm_redraw_last(void);
        extern struct task *task_find_foreground(void);
        extern void signal_send_subtree(struct task *root, int sig);
        if (g_fb_drm[n]) drm_redraw_last();
        else             fb_unpark(n);
        struct task *fg = task_find_foreground();
        if (fg) signal_send_subtree(fg, 28);
    }
}

void vt_write(int n, const char *buf, size_t len) {
    if (!g_inited || !buf || len == 0) return;
    if (n < 0 || n >= VT_COUNT || g_vts[n].is_monitor) return;

    uint64_t f = spinlock_acquire_irqsave(&g_lock);

    if (ensure_grid(n) < 0) { spinlock_release_irqrestore(&g_lock, f); return; }

    if (n == g_active) {
        erase_cursor();
        putchar_flush_begin();
        for (size_t i = 0; i < len; i++) putchar((int)(unsigned char)buf[i]);
        putchar_flush_end();
        draw_cursor();
        g_blink_on = 1;
        g_blink_next = sched_now_ns() + BLINK_PERIOD_NS;
        char rep[48]; int rn = console_take_reply(rep, sizeof rep);
        spinlock_release_irqrestore(&g_lock, f);
        for (int i = 0; i < rn; i++) tty_vt_input(n, rep[i]);
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
    console_set_offscreen(g_fb_task[g_active] != NULL);

    console_save_state(&g_vts[n].state);
    console_set_grid(g_vts[g_active].grid, g_cols, g_rows);
    console_load_state(&saved);

    char rep[48]; int rn = console_take_reply(rep, sizeof rep);
    spinlock_release_irqrestore(&g_lock, f);
    for (int i = 0; i < rn; i++) tty_vt_input(n, rep[i]);
}

static volatile int g_switch_req = -1;
static int g_switch_worker;

static void vt_switch_worker(void *arg) {
    (void)arg;
    for (;;) {
        int n = __atomic_exchange_n(&g_switch_req, -1, __ATOMIC_ACQ_REL);
        if (n >= 0) vt_switch(n);
        task_sleep_ms(5);
    }
}

void vt_start_worker(void) {
    if (task_create("vt_switch", vt_switch_worker, NULL, 1)) g_switch_worker = 1;
}

void vt_request_switch(int n) {
    if (!g_switch_worker) { vt_switch(n); return; }
    __atomic_store_n(&g_switch_req, n, __ATOMIC_RELEASE);
}

void vt_handle_chord(int fn) {
    if (fn < 1 || fn > VT_COUNT) return;
    vt_request_switch(fn - 1);
}

void vt_tick_flush(void) {
    if (!g_inited) return;
    if (!spinlock_try_acquire(&g_lock)) return;
    console_flush_pending();
    if (!g_vts[g_active].is_monitor && console_cursor_visible()) {
        uint64_t now = sched_now_ns();
        if (now >= g_blink_next) {
            g_blink_on = !g_blink_on;
            if (g_blink_on) draw_cursor();
            else            erase_cursor();
            g_blink_next = now + BLINK_PERIOD_NS;
        }
    }
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
        if (row) *row = g_vts[vt].state.cursor_y / fb_font_height();
        if (col) *col = g_vts[vt].state.cursor_x / fb_font_width();
    }
    spinlock_release_irqrestore(&g_lock, f);
}

void console_input_char(char c) {
    if (g_inited && g_active == VT_MONITOR_INDEX) {
        monitor_input(c);
        return;
    }
    if (vt_kbd_muted()) return;
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

void vt_theme_changed(const uint32_t old_pal[16], uint32_t old_fg, uint32_t old_bg) {
    if (!g_inited || !global_framebuffer) return;
    uint64_t f = spinlock_acquire_irqsave(&g_lock);

    for (int i = 0; i < VT_COUNT; i++) {
        if (g_vts[i].grid) {
            size_t n = (size_t)g_cols * g_rows;
            for (size_t c = 0; c < n; c++) {
                g_vts[i].grid[c].fg = console_theme_remap(g_vts[i].grid[c].fg,
                                                          old_pal, old_fg, old_bg);
                g_vts[i].grid[c].bg = console_theme_remap(g_vts[i].grid[c].bg,
                                                          old_pal, old_fg, old_bg);
            }
        }
        g_vts[i].state.text_color = console_theme_remap(g_vts[i].state.text_color,
                                                        old_pal, old_fg, old_bg);
        g_vts[i].state.bg_color   = console_theme_remap(g_vts[i].state.bg_color,
                                                        old_pal, old_fg, old_bg);
    }

    console_save_state(&g_vts[g_active].state);
    if (global_framebuffer)
        fb_fill_rect(global_framebuffer, 0, 0,
                     global_framebuffer->width, global_framebuffer->height,
                     console_theme_bg());
    console_redraw_grid();
    fb_flush(global_framebuffer);
    spinlock_release_irqrestore(&g_lock, f);
}

void vt_font_changed(void) {
    if (!g_inited || !global_framebuffer) return;
    uint32_t nc = (uint32_t)(global_framebuffer->width  / fb_font_width());
    uint32_t nr = (uint32_t)(global_framebuffer->height / fb_font_height());
    if (nc < 1) nc = 1;
    if (nr < 1) nr = 1;

    uint64_t f = spinlock_acquire_irqsave(&g_lock);

    uint32_t oc = g_cols, orow = g_rows;
    uint32_t ocw = (oc    > 0) ? (uint32_t)(global_framebuffer->width  / oc)   : fb_font_width();
    uint32_t och = (orow  > 0) ? (uint32_t)(global_framebuffer->height / orow) : fb_font_height();
    if (ocw < 1) ocw = 1;
    if (och < 1) och = 1;
    uint32_t ncw = fb_font_width(), nch = fb_font_height();

    if (!g_vts[g_active].is_monitor && g_vts[g_active].grid)
        console_save_state(&g_vts[g_active].state);

    g_cols = nc;
    g_rows = nr;

    uint32_t cc = (oc   < nc) ? oc   : nc;
    uint32_t cr = (orow < nr) ? orow : nr;

    for (int i = 0; i < VT_COUNT; i++) {
        if (g_vts[i].is_monitor || !g_vts[i].grid) continue;

        vt_cell_t *og = g_vts[i].grid;
        vt_cell_t *ng = grid_alloc();
        if (!ng) { kfree(og); g_vts[i].grid = NULL; continue; }
        for (uint32_t r = 0; r < cr; r++)
            for (uint32_t c = 0; c < cc; c++)
                ng[(size_t)r * nc + c] = og[(size_t)r * oc + c];
        kfree(og);
        g_vts[i].grid = ng;

        uint32_t col = g_vts[i].state.cursor_x / ocw;
        uint32_t row = g_vts[i].state.cursor_y / och;
        if (col >= nc) col = nc - 1;
        if (row >= nr) row = nr - 1;
        g_vts[i].state.cursor_x = col * ncw;
        g_vts[i].state.cursor_y = row * nch;
    }
    if (!g_vts[g_active].is_monitor && g_vts[g_active].grid) {
        console_set_grid(g_vts[g_active].grid, g_cols, g_rows);
        console_load_state(&g_vts[g_active].state);
        fb_clear(global_framebuffer, 0);
        console_redraw_grid();
        fb_flush(global_framebuffer);
    }
    spinlock_release_irqrestore(&g_lock, f);

    extern struct task *task_find_foreground(void);
    extern void signal_send_subtree(struct task *root, int sig);
    struct task *fg = task_find_foreground();
    if (fg) signal_send_subtree(fg, 28);
}
