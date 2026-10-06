#include <fenv.h>
#include <math.h>
#include <stdint.h>

float fmaf(float x, float y, float z)
{
	#pragma STDC FENV_ACCESS ON
	double xy, result;
	union {double f; uint64_t i;} u;
	int e;

	xy = (double)x * y;
	result = xy + z;
	u.f = result;
	e = u.i>>52 & 0x7ff;

	if ((u.i & 0x1fffffff) != 0x10000000 ||
		e == 0x7ff ||
		(result - xy == z && result - z == xy) ||
		fegetround() != FE_TONEAREST)
	{
#if defined(FE_INEXACT) && defined(FE_UNDERFLOW)
		if (e < 0x3ff-126 && e >= 0x3ff-149 && fetestexcept(FE_INEXACT)) {
			feclearexcept(FE_INEXACT);

			volatile float vz = z;
			result = xy + vz;
			if (fetestexcept(FE_INEXACT))
				feraiseexcept(FE_UNDERFLOW);
			else
				feraiseexcept(FE_INEXACT);
		}
#endif
		z = result;
		return z;
	}

	double err;
	int neg = u.i >> 63;
	if (neg == (z > xy))
		err = xy - result + z;
	else
		err = z - result + xy;
	if (neg == (err < 0))
		u.i++;
	else
		u.i--;
	z = u.f;
	return z;
}
