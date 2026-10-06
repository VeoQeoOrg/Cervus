#include <limits.h>
#include <fenv.h>
#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long lrintl(long double x)
{
	return lrint(x);
}
#elif defined(FE_INEXACT)

long lrintl(long double x)
{
	#pragma STDC FENV_ACCESS ON
	int e;

	e = fetestexcept(FE_INEXACT);
	x = rintl(x);
	if (!e && (x > LONG_MAX || x < LONG_MIN))
		feclearexcept(FE_INEXACT);

	return x;
}
#else
long lrintl(long double x)
{
	return rintl(x);
}
#endif
