#include "stdio_impl.h"
#include <libcervus.h>

static FILE *volatile dummy = 0;
weak_alias(dummy, __stdin_used);
weak_alias(dummy, __stdout_used);
weak_alias(dummy, __stderr_used);

static FILE *ofl_head;
static __cervus_lock_t ofl_lock = CERVUS_LOCK_INIT;

FILE **__ofl_lock()
{
	__cervus_lock(&ofl_lock);
	return &ofl_head;
}

void __ofl_unlock()
{
	__cervus_unlock(&ofl_lock);
}

static int fork_tid;

static void fork_reset_lock(FILE *f, int tid)
{
	if (!f || f->lock <= 0) return;
	f->lock = (f->lock & ~MAYBE_WAITERS) == fork_tid ? tid : 0;
}

void __cervus_stdio_atfork(int phase)
{
	if (phase < 0) {
		fork_tid = __cervus_tid();
		__cervus_lock(&ofl_lock);
		return;
	}
	if (phase > 0) {
		int tid = __cervus_tid();
		fork_reset_lock(__stdin_used, tid);
		fork_reset_lock(__stdout_used, tid);
		fork_reset_lock(__stderr_used, tid);
		for (FILE *f = ofl_head; f; f = f->next) fork_reset_lock(f, tid);
	}
	__cervus_unlock(&ofl_lock);
}

static void init_file_lock(FILE *f)
{
	if (f && f->lock<0) f->lock = 0;
}

void __cervus_stdio_threaded(void)
{
	for (FILE *f=*__ofl_lock(); f; f=f->next)
		init_file_lock(f);
	__ofl_unlock();
	init_file_lock(__stdin_used);
	init_file_lock(__stdout_used);
	init_file_lock(__stderr_used);
}
