#include <math.h>
#include <float.h>

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double fminl(long double x, long double y)
{
	return fmin(x, y);
}
#else
long double fminl(long double x, long double y)
{
	if (isnan(x))
		return y;
	if (isnan(y))
		return x;

	if (signbit(x) != signbit(y))
		return signbit(x) ? x : y;
	return x < y ? x : y;
}
#endif
