#include "../../include/console/klog.h"
#include "../../include/sched/spinlock.h"
#include <string.h>

static char       g_lines[KLOG_MAX_LINES][KLOG_LINE_MAX];
static uint16_t   g_len[KLOG_MAX_LINES];
static uint64_t   g_total;
static spinlock_t g_lock = SPINLOCK_INIT;
static klog_notify_fn g_notify;
static volatile int   g_in_notify;

static void put_locked(char c) {
    if (c == '\r') return;
    if (c == '\n') {
        g_total++;
        g_len[g_total % KLOG_MAX_LINES] = 0;
        return;
    }
    uint64_t idx = g_total % KLOG_MAX_LINES;
    if (g_len[idx] < KLOG_LINE_MAX - 1)
        g_lines[idx][g_len[idx]++] = c;
}

static void fire_notify(void) {
    if (g_notify && !g_in_notify) {
        g_in_notify = 1;
        g_notify();
        g_in_notify = 0;
    }
}

void klog_putc(char c) {
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    put_locked(c);
    spinlock_release_irqrestore(&g_lock, f);
    if (c == '\n') fire_notify();
}

void klog_write(const char *buf, size_t len) {
    if (!buf || len == 0) return;
    int saw_nl = 0;
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    for (size_t i = 0; i < len; i++) {
        put_locked(buf[i]);
        if (buf[i] == '\n') saw_nl = 1;
    }
    spinlock_release_irqrestore(&g_lock, f);
    if (saw_nl) fire_notify();
}

void klog_set_notify(klog_notify_fn fn) { g_notify = fn; }

uint64_t klog_total(void) { return g_total; }

uint64_t klog_first(void) {
    if (g_total >= KLOG_MAX_LINES) return g_total - (KLOG_MAX_LINES - 1);
    return 0;
}

int klog_get_line(uint64_t line_no, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return -1;
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    uint64_t first = (g_total >= KLOG_MAX_LINES) ? g_total - (KLOG_MAX_LINES - 1) : 0;
    if (line_no > g_total || line_no < first) {
        spinlock_release_irqrestore(&g_lock, f);
        return -1;
    }
    uint64_t idx = line_no % KLOG_MAX_LINES;
    size_t n = g_len[idx];
    if (n > out_sz - 1) n = out_sz - 1;
    memcpy(out, g_lines[idx], n);
    out[n] = 0;
    spinlock_release_irqrestore(&g_lock, f);
    return (int)n;
}
