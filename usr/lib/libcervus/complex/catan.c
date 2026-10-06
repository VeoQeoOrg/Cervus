#include "complex_impl.h"

double complex catan(double complex z)
{
	double complex w;
	double a, t, x, x2, y;

	x = creal(z);
	y = cimag(z);

	x2 = x * x;
	a = 1.0 - x2 - (y * y);

	t = 0.5 * atan2(2.0 * x, a);
	w = t;

	t = y - 1.0;
	a = x2 + (t * t);

	t = y + 1.0;
	a = (x2 + t * t)/a;
	w = CMPLX(w, 0.25 * log(a));
	return w;
}
