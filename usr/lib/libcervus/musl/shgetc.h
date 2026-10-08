#ifndef _CERVUS_SHGETC_H
#define _CERVUS_SHGETC_H

#include <sys/types.h>
#include "musl.h"

typedef struct {
	unsigned char *buf, *rpos, *rend, *shend;
	off_t shlim, shcnt;
} SHFILE;

hidden void __shlim(SHFILE *, off_t);
hidden int __shgetc(SHFILE *);

#define shcnt(f) ((f)->shcnt + ((f)->rpos - (f)->buf))
#define shlim(f, lim) __shlim((f), (lim))
#define shgetc(f) (((f)->rpos != (f)->shend) ? *(f)->rpos++ : __shgetc(f))
#define shunget(f) ((f)->shlim>=0 ? (void)(f)->rpos-- : (void)0)

#define sh_fromstring(f, s) \
	((f)->buf = (f)->rpos = (void *)(s), (f)->rend = (void*)-1)

#endif
