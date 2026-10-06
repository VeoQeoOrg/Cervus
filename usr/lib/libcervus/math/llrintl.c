#include <limits.h>
#include <fenv.h>
#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long long llrintl(long double x)
{
	return llrint(x);
}
#elif defined(FE_INEXACT)

long long llrintl(long double x)
{
	#pragma STDC FENV_ACCESS ON
	int e;

	e = fetestexcept(FE_INEXACT);
	x = rintl(x);
	if (!e && (x > LLONG_MAX || x < LLONG_MIN))
		feclearexcept(FE_INEXACT);

	return x;
}
#else
long long llrintl(long double x)
{
	return rintl(x);
}
#endif
