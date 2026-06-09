#ifndef CLOCKSOURCE_H
#define CLOCKSOURCE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct clocksource {
    const char *name;
    int         rating;
    bool      (*available)(void);
    uint64_t  (*read_ns)(void);
    int64_t     offset_ns;
    bool        unstable;
    struct clocksource *next;
} clocksource_t;

void          clocksource_register(clocksource_t *cs);
void          clocksource_init(void);
void          clocksource_select(void);
clocksource_t *clocksource_current(void);
clocksource_t *clocksource_watchdog(void);
uint64_t      clocksource_now_ns(void);
void          clocksource_watchdog_tick(void);
int           clocksource_list(char *buf, int max);

#endif
