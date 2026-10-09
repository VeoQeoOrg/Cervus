#include "stdio_impl.h"

hidden void __shlim(FILE *, off_t);
hidden int __shgetc(FILE *);

#define shcnt(f) ((f)->shcnt + ((f)->rpos - (f)->buf))
#define shlim(f, lim) __shlim((f), (lim))
#define shgetc(f) (((f)->rpos != (f)->shend) ? *(f)->rpos++ : __shgetc(f))
#define shunget(f) ((f)->shlim>=0 ? (void)(f)->rpos-- : (void)0)

#define sh_fromstring(f, s) \
	((f)->buf = (f)->rpos = (void *)(s), (f)->rend = (void*)-1)
