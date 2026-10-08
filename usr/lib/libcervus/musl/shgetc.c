#include <stdio.h>
#include "shgetc.h"

void __shlim(SHFILE *f, off_t lim)
{
	f->shlim = lim;
	f->shcnt = f->buf - f->rpos;
	if (lim && f->rend - f->rpos > lim)
		f->shend = f->rpos + lim;
	else
		f->shend = f->rend;
}

int __shgetc(SHFILE *f)
{
	off_t cnt = shcnt(f);
	f->shcnt = f->buf - f->rpos + cnt;
	f->shend = f->rpos;
	f->shlim = -1;
	return EOF;
}
