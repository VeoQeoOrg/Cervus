#include "stdio_impl.h"
#include <sys/syscall.h>
#include <libcervus.h>

static int a_cas(volatile int *p, int t, int s)
{
	__atomic_compare_exchange_n(p, &t, s, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return t;
}

int __lockfile(FILE *f)
{
	int owner = f->lock, tid = __cervus_tid();
	if ((owner & ~MAYBE_WAITERS) == tid)
		return 0;
	owner = a_cas(&f->lock, 0, tid);
	if (!owner) return 1;
	while ((owner = a_cas(&f->lock, 0, tid|MAYBE_WAITERS))) {
		if ((owner & MAYBE_WAITERS) ||
		    a_cas(&f->lock, owner, owner|MAYBE_WAITERS)==owner)
			syscall3(SYS_FUTEX_WAIT, (uint64_t)(uintptr_t)&f->lock, (uint64_t)(owner|MAYBE_WAITERS), 0);
	}
	return 1;
}

void __unlockfile(FILE *f)
{
	if (__atomic_exchange_n(&f->lock, 0, __ATOMIC_SEQ_CST) & MAYBE_WAITERS)
		syscall2(SYS_FUTEX_WAKE, (uint64_t)(uintptr_t)&f->lock, 1);
}
