#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double expl(long double x)
{
	return exp(x);
}
#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384

static const long double P[3] = {
 1.2617719307481059087798E-4L,
 3.0299440770744196129956E-2L,
 9.9999999999999999991025E-1L,
};
static const long double Q[4] = {
 3.0019850513866445504159E-6L,
 2.5244834034968410419224E-3L,
 2.2726554820815502876593E-1L,
 2.0000000000000000000897E0L,
};
static const long double
LN2HI = 6.9314575195312500000000E-1L,
LN2LO = 1.4286068203094172321215E-6L,
LOG2E = 1.4426950408889634073599E0L;

long double expl(long double x)
{
	long double px, xx;
	int k;

	if (isnan(x))
		return x;
	if (x > 11356.5234062941439488L)
		return x * 0x1p16383L;
	if (x < -11399.4985314888605581L)
		return -0x1p-16445L/x;

	px = floorl(LOG2E * x + 0.5);
	k = px;
	x -= px * LN2HI;
	x -= px * LN2LO;

	xx = x * x;
	px = x * __polevll(xx, P, 2);
	x = px/(__polevll(xx, Q, 3) - px);
	x = 1.0 + 2.0 * x;
	return scalbnl(x, k);
}
#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384

long double expl(long double x)
{
	return exp(x);
}
#endif
