#ifndef _CERVUS_SHGETC_H
#define _CERVUS_SHGETC_H

#include <stdio.h>
#include <sys/types.h>
#include "musl.h"

typedef struct {
	unsigned char *buf, *rpos, *rend, *shend;
	off_t shlim, shcnt;
	FILE *file;
	int last;
} SHFILE;

hidden void __shlim(SHFILE *, off_t);
hidden int __shgetc(SHFILE *);
hidden void __shunget(SHFILE *);
hidden int __cervus_vscan(SHFILE *restrict, const char *restrict, __builtin_va_list);

#define shcnt(f) ((f)->shcnt + ((f)->rpos - (f)->buf))
#define shlim(f, lim) __shlim((f), (lim))
#define shgetc(f) (((f)->rpos != (f)->shend) ? *(f)->rpos++ : __shgetc(f))
#define shunget(f) ((f)->file ? __shunget(f) : (f)->shlim>=0 ? (void)(f)->rpos-- : (void)0)

#define sh_fromstring(f, s) \
	((f)->buf = (f)->rpos = (void *)(s), (f)->rend = (void*)-1, (f)->file = 0)

#define sh_fromfile(f, fp) \
	((f)->buf = (f)->rpos = (f)->rend = (f)->shend = 0, (f)->shcnt = 0, \
	 (f)->shlim = 0, (f)->file = (fp), (f)->last = -1)

#endif
