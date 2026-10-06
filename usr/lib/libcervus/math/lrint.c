#include <limits.h>
#include <fenv.h>
#include <math.h>
#include "libm.h"

#if LONG_MAX < 1U<<53 && defined(FE_INEXACT)
#include <float.h>
#include <stdint.h>
#if FLT_EVAL_METHOD==0 || FLT_EVAL_METHOD==1
#define EPS DBL_EPSILON
#elif FLT_EVAL_METHOD==2
#define EPS LDBL_EPSILON
#endif
#ifdef __GNUC__

__attribute__((noinline))
#endif
static long lrint_slow(double x)
{
	#pragma STDC FENV_ACCESS ON
	int e;

	e = fetestexcept(FE_INEXACT);
	x = rint(x);
	if (!e && (x > LONG_MAX || x < LONG_MIN))
		feclearexcept(FE_INEXACT);

	return x;
}

long lrint(double x)
{
	uint32_t abstop = asuint64(x)>>32 & 0x7fffffff;
	uint64_t sign = asuint64(x) & (1ULL << 63);

	if (abstop < 0x41dfffff) {
		double_t toint = asdouble(asuint64(1/EPS) | sign);
		double_t y = x + toint - toint;
		return (long)y;
	}
	return lrint_slow(x);
}
#else
long lrint(double x)
{
	return rint(x);
}
#endif
