#include "stdio_impl.h"
#include <limits.h>

void __do_orphaned_stdio_locks()
{
	FILE *f;
	for (f=*__cervus_stdio_locks(); f; f=f->next_locked)
		__atomic_store_n(&f->lock, 0x40000000, __ATOMIC_SEQ_CST);
}

void __unlist_locked_file(FILE *f)
{
	if (f->lockcount) {
		if (f->next_locked) f->next_locked->prev_locked = f->prev_locked;
		if (f->prev_locked) f->prev_locked->next_locked = f->next_locked;
		else *__cervus_stdio_locks() = f->next_locked;
	}
}

void __register_locked_file(FILE *f)
{
	FILE **head = __cervus_stdio_locks();
	f->lockcount = 1;
	f->prev_locked = 0;
	f->next_locked = *head;
	if (f->next_locked) f->next_locked->prev_locked = f;
	*head = f;
}

int ftrylockfile(FILE *f)
{
	int tid = __cervus_tid();
	int owner = f->lock;
	if ((owner & ~MAYBE_WAITERS) == tid) {
		if (f->lockcount == LONG_MAX)
			return -1;
		f->lockcount++;
		return 0;
	}
	if (owner < 0) f->lock = owner = 0;
	if (owner) return -1;
	int expect = 0;
	if (!__atomic_compare_exchange_n(&f->lock, &expect, tid, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return -1;
	__register_locked_file(f);
	return 0;
}
