#ifndef _SYS_FUTEX_H
#define _SYS_FUTEX_H

#include <stdint.h>

int futex_wait(volatile uint32_t *addr, uint32_t expected, uint64_t timeout_ns);
int futex_wake(volatile uint32_t *addr, int count);

void futex_lock(volatile uint32_t *lock);
void futex_unlock(volatile uint32_t *lock);
int  futex_trylock(volatile uint32_t *lock);

#endif
