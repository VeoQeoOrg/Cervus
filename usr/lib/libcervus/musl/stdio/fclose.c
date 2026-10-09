#include "stdio_impl.h"
#include <stdlib.h>

static void dummy(FILE *f) { }
weak_alias(dummy, __unlist_locked_file);

int fclose(FILE *f)
{
	int r;

	FLOCK(f);
	r = fflush(f);
	r |= f->close(f);
	FUNLOCK(f);

	if (f->flags & F_PERM) return r;

	__unlist_locked_file(f);

	FILE **head = __ofl_lock();
	if (f->prev) f->prev->next = f->next;
	if (f->next) f->next->prev = f->prev;
	if (*head == f) *head = f->next;
	__ofl_unlock();

	free(f->getln_buf);
	free(f);

	return r;
}
