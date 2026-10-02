#include "../include/sched/sched.h"
#include "../include/memory/pmm.h"
#include "../include/io/serial.h"
#include <string.h>

#define PROF_CAP    65536
#define PROF_SLOTS  4096
#define PROF_TOP    32

extern const char *ksym_lookup(uint64_t addr, uint64_t *off_out);

typedef struct { uint64_t key; uint32_t count; } prof_slot_t;

void profile_start(task_t *t) {
    if (t->prof_buf) return;
    t->prof_buf = kmalloc(PROF_CAP * sizeof(uint64_t));
    t->prof_n = 0;
}

void profile_tick(task_t *t, uint64_t rip) {
    uint64_t *b = t->prof_buf;
    if (!b) return;
    uint32_t n = t->prof_n;
    if (n < PROF_CAP) { b[n] = rip; t->prof_n = n + 1; }
}

static void add(prof_slot_t *tab, uint64_t key) {
    uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ULL) >> 52) % PROF_SLOTS;
    for (uint32_t i = 0; i < PROF_SLOTS; i++) {
        prof_slot_t *s = &tab[(h + i) % PROF_SLOTS];
        if (s->count && s->key == key) { s->count++; return; }
        if (!s->count) { s->key = key; s->count = 1; return; }
    }
}

static void report(prof_slot_t *tab, const char *what, uint32_t total, bool kernel) {
    for (int k = 0; k < PROF_TOP; k++) {
        int best = -1;
        for (int i = 0; i < PROF_SLOTS; i++)
            if (tab[i].count && (best < 0 || tab[i].count > tab[best].count)) best = i;
        if (best < 0) break;
        prof_slot_t *s = &tab[best];
        uint32_t pct10 = (uint32_t)((uint64_t)s->count * 1000 / (total ? total : 1));
        if (kernel) {
            const char *name = (const char *)(uintptr_t)s->key;
            serial_printf("[profile]   %s %5u %3u.%u%%  %s\n", what, s->count, pct10 / 10, pct10 % 10,
                          name ? name : "?");
        } else {
            serial_printf("[profile]   %s %5u %3u.%u%%  0x%llx\n", what, s->count, pct10 / 10, pct10 % 10,
                          (unsigned long long)s->key);
        }
        s->count = 0;
    }
}

void profile_finish(task_t *t) {
    uint64_t *b = t->prof_buf;
    if (!b) return;
    t->prof_buf = NULL;
    uint32_t n = t->prof_n;
    prof_slot_t *ktab = kzalloc(PROF_SLOTS * sizeof(prof_slot_t));
    prof_slot_t *utab = kzalloc(PROF_SLOTS * sizeof(prof_slot_t));
    if (ktab && utab) {
        uint32_t kn = 0, un = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t rip = b[i];
            if (rip >= 0xffff800000000000ULL) {
                const char *name = ksym_lookup(rip, NULL);
                add(ktab, (uint64_t)(uintptr_t)name);
                kn++;
            } else {
                add(utab, rip & ~0x3FULL);
                un++;
            }
        }
        serial_printf("[profile] pid %u '%s': %u samples at 1 kHz, %u in the kernel, %u in user space\n",
                      t->pid, t->name, n, kn, un);
        report(ktab, "kernel", n, true);
        report(utab, "user  ", n, false);
    }
    kfree(ktab);
    kfree(utab);
    kfree(b);
}
