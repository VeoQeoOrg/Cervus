#include <complex.h>
#include <float.h>
#include "complex_impl.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double complex catanl(long double complex z)
{
	return catan(z);
}
#else
long double complex catanl(long double complex z)
{
	long double complex w;
	long double a, t, x, x2, y;

	x = creall(z);
	y = cimagl(z);

	x2 = x * x;
	a = 1.0L - x2 - (y * y);

	t = atan2l(2.0L * x, a) * 0.5L;
	w = t;

	t = y - 1.0L;
	a = x2 + (t * t);

	t = y + 1.0L;
	a = (x2 + (t * t)) / a;
	w = CMPLXF(w, 0.25L * logl(a));
	return w;
}
#endif
