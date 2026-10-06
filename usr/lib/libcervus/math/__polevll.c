#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
#else

long double __polevll(long double x, const long double *P, int n)
{
	long double y;

	y = *P++;
	do {
		y = y * x + *P++;
	} while (--n);

	return y;
}

long double __p1evll(long double x, const long double *P, int n)
{
	long double y;

	n -= 1;
	y = x + *P++;
	do {
		y = y * x + *P++;
	} while (--n);

	return y;
}
#endif
