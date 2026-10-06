#include <math.h>

long double remquol(long double x, long double y, int *quo)
{
	signed char *cx = (void *)&x, *cy = (void *)&y;

	__asm__ ("" :: "X"(cx), "X"(cy));

	long double t = x;
	unsigned fpsr;
	do __asm__ ("fprem1; fnstsw %%ax" : "+t"(t), "=a"(fpsr) : "u"(y));
	while (fpsr & 0x400);

	unsigned char i = fpsr >> 8;
	i = i>>4 | i<<4;

	unsigned qbits = 0x7575313164642020 >> (i & 60);
	qbits &= 7;

	*quo = (cx[9]^cy[9]) < 0 ? -qbits : qbits;
	return t;
}
