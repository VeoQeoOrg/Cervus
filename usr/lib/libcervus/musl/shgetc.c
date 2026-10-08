#include <stdio.h>
#include "shgetc.h"

void __shlim(SHFILE *f, off_t lim)
{
	f->shlim = lim;
	if (f->file) {
		f->shcnt = 0;
		return;
	}
	f->shcnt = f->buf - f->rpos;
	if (lim && f->rend - f->rpos > lim)
		f->shend = f->rpos + lim;
	else
		f->shend = f->rend;
}

int __shgetc(SHFILE *f)
{
	if (f->file) {
		int c;
		if ((f->shlim && f->shcnt >= f->shlim) || (c = getc_unlocked(f->file)) == EOF) {
			f->shlim = -1;
			f->last = -1;
			return EOF;
		}
		f->shcnt++;
		f->last = c;
		return c;
	}
	off_t cnt = shcnt(f);
	f->shcnt = f->buf - f->rpos + cnt;
	f->shend = f->rpos;
	f->shlim = -1;
	return EOF;
}

void __shunget(SHFILE *f)
{
	if (f->last < 0) return;
	ungetc(f->last, f->file);
	f->shcnt--;
	f->last = -1;
}
