#include "../../include/time/clocksource.h"
#include "../../include/apic/apic.h"
#include "../../include/drivers/timer.h"
#include "../../include/io/serial.h"
#include <stdio.h>
#include <stddef.h>

static clocksource_t *g_cs_list     = NULL;
static clocksource_t *g_cs_current  = NULL;
static clocksource_t *g_cs_watchdog = NULL;

static volatile uint64_t g_mono_last = 0;

static uint64_t g_wd_last_cur = 0;
static uint64_t g_wd_last_wd  = 0;
static bool     g_wd_armed    = false;
static int      g_wd_strikes  = 0;

static bool cs_tsc_available(void)  { return tsc_elapsed_ns() != 0; }
static uint64_t cs_tsc_read(void)   { return tsc_elapsed_ns(); }

static bool cs_hpet_available(void) { return hpet_is_available(); }
static uint64_t cs_hpet_read(void)  { return hpet_elapsed_ns(); }

static bool cs_jiffies_available(void) { return true; }
static uint64_t cs_jiffies_read(void)  { return timer_get_ticks() * 1000000ULL; }

static clocksource_t cs_tsc = {
    .name = "tsc", .rating = 200,
    .available = cs_tsc_available, .read_ns = cs_tsc_read,
};

static clocksource_t cs_hpet = {
    .name = "hpet", .rating = 250,
    .available = cs_hpet_available, .read_ns = cs_hpet_read,
};

static clocksource_t cs_jiffies = {
    .name = "jiffies", .rating = 100,
    .available = cs_jiffies_available, .read_ns = cs_jiffies_read,
};

void clocksource_register(clocksource_t *cs) {
    cs->next  = g_cs_list;
    g_cs_list = cs;
}

static uint64_t cs_now(clocksource_t *cs) {
    return cs->read_ns() + (uint64_t)cs->offset_ns;
}

void clocksource_select(void) {
    clocksource_t *best = NULL, *second = NULL;
    for (clocksource_t *cs = g_cs_list; cs; cs = cs->next) {
        if (cs->unstable || !cs->available()) continue;
        if (!best || cs->rating > best->rating) {
            if (best && best->rating >= 100) second = best;
            best = cs;
        } else if ((!second || cs->rating > second->rating) && cs->rating >= 100) {
            second = cs;
        }
    }

    if (!best) return;

    if (best != g_cs_current) {
        uint64_t base = __atomic_load_n(&g_mono_last, __ATOMIC_RELAXED);
        if (!base && g_cs_current) base = cs_now(g_cs_current);
        if (base)
            best->offset_ns = (int64_t)base - (int64_t)best->read_ns();
        g_cs_current = best;
        serial_printf("[time] clocksource: %s (rating %d)%s\n",
                      best->name, best->rating,
                      second ? "" : ", no watchdog");
    }
    g_cs_watchdog = second;
    g_wd_armed = false;
    g_wd_strikes = 0;
}

clocksource_t *clocksource_current(void)  { return g_cs_current; }
clocksource_t *clocksource_watchdog(void) { return g_cs_watchdog; }

uint64_t clocksource_now_ns(void) {
    clocksource_t *cs = g_cs_current;
    uint64_t now;
    if (cs) {
        now = cs_now(cs);
    } else {
        now = tsc_elapsed_ns();
        if (!now) now = timer_get_ticks() * 1000000ULL;
    }

    uint64_t last = __atomic_load_n(&g_mono_last, __ATOMIC_RELAXED);
    while (now > last) {
        if (last != 0 && now - last > 3600000000000ULL) {
            static int poisoned_logged = 0;
            if (!poisoned_logged) {
                poisoned_logged = 1;
                serial_printf("[time] REJECTED insane forward jump: %llu -> %llu ns\n",
                              (unsigned long long)last, (unsigned long long)now);
            }
            if (cs) {
                cs->unstable = true;
                clocksource_select();
            }
            return last;
        }
        if (__atomic_compare_exchange_n(&g_mono_last, &last, now, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            return now;
    }
    return last;
}

void clocksource_watchdog_tick(void) {
    clocksource_t *cur = g_cs_current;
    clocksource_t *wd  = g_cs_watchdog;
    if (!cur || !wd) return;

    uint64_t cur_now = cs_now(cur);
    uint64_t wd_now  = wd->read_ns();

    if (!g_wd_armed) {
        g_wd_last_cur = cur_now;
        g_wd_last_wd  = wd_now;
        g_wd_armed = true;
        return;
    }

    uint64_t cur_d = cur_now - g_wd_last_cur;
    uint64_t wd_d  = wd_now  - g_wd_last_wd;
    g_wd_last_cur = cur_now;
    g_wd_last_wd  = wd_now;

    uint64_t diff = (cur_d > wd_d) ? (cur_d - wd_d) : (wd_d - cur_d);
    bool wd_is_ticks = (wd == &cs_jiffies);
    uint64_t margin = wd_is_ticks ? wd_d / 20 : wd_d / 100;
    if (margin < 10000000ULL) margin = 10000000ULL;

    bool suspect = (diff > margin);
    if (suspect && wd_is_ticks && cur_d > wd_d)
        suspect = false;

    if (suspect) {
        g_wd_strikes++;
        serial_printf("[time] WATCHDOG: '%s' drifted %llu ns vs '%s' over %llu ns "
                      "(strike %d)\n",
                      cur->name, (unsigned long long)diff, wd->name,
                      (unsigned long long)wd_d, g_wd_strikes);
        if (g_wd_strikes >= 2) {
            serial_printf("[time] WATCHDOG: marking '%s' unstable\n", cur->name);
            cur->unstable = true;
            clocksource_select();
        }
    } else {
        g_wd_strikes = 0;
    }
}

int clocksource_list(char *buf, int max) {
    int off = 0;
    for (clocksource_t *cs = g_cs_list; cs && off < max; cs = cs->next) {
        int w = snprintf(buf + off, max - off, "%s%s rating=%d%s%s\n",
                         (cs == g_cs_current) ? "* " :
                         (cs == g_cs_watchdog) ? "w " : "  ",
                         cs->name, cs->rating,
                         cs->available() ? "" : " (unavailable)",
                         cs->unstable ? " (unstable)" : "");
        if (w < 0) break;
        off += w;
    }
    return off;
}

void clocksource_init(void) {
    if (tsc_is_invariant()) {
        cs_tsc.rating = 300;
        serial_printf("[time] TSC is invariant (rating 300)\n");
    } else {
        cs_tsc.rating = 50;
        serial_printf("[time] TSC is NOT invariant (halts in C-states), rating 50\n");
    }
    clocksource_register(&cs_jiffies);
    clocksource_register(&cs_hpet);
    clocksource_register(&cs_tsc);
    clocksource_select();
}
