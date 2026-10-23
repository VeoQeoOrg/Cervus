#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>

bool timer_init(void);
void timer_start_recal_task(void);

uint64_t timer_get_ticks(void);

void tick_nohz_idle_enter(uint32_t cpu);
void tick_nohz_restart(void);
void tick_nohz_kick(uint64_t deadline_ns);

uint64_t sched_now_ns(void);

void timer_sleep_ms(uint64_t milliseconds);
void timer_sleep_us(uint64_t microseconds);
void timer_sleep_ns(uint64_t nanoseconds);

extern volatile uint32_t g_ctrlc_pending;

#endif