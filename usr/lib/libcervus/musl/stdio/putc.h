#include "stdio_impl.h"

__attribute__((__noinline__))
static int locking_putc(int c, FILE *f)
{
	int expect = 0;
	if (!__atomic_compare_exchange_n(&f->lock, &expect, MAYBE_WAITERS-1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		__lockfile(f);
	c = putc_unlocked(c, f);
	__unlockfile(f);
	return c;
}

static inline int do_putc(int c, FILE *f)
{
	int l = f->lock;
	if (l < 0 || l && (l & ~MAYBE_WAITERS) == __cervus_tid())
		return putc_unlocked(c, f);
	return locking_putc(c, f);
}
