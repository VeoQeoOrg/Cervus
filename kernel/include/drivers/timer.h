#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>

bool timer_init(void);

uint64_t timer_get_ticks(void);

void timer_sleep_ms(uint64_t milliseconds);
void timer_sleep_us(uint64_t microseconds);
void timer_sleep_ns(uint64_t nanoseconds);

#endif