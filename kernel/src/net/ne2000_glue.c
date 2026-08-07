#include "../../include/sched/spinlock.h"

static spinlock_t g_ne2000_lock = SPINLOCK_INIT;

void ne2000_lock(void) { spinlock_acquire(&g_ne2000_lock); }
void ne2000_unlock(void) { spinlock_release(&g_ne2000_lock); }
