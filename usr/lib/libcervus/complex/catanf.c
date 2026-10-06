#include "complex_impl.h"

float complex catanf(float complex z)
{
	float complex w;
	float a, t, x, x2, y;

	x = crealf(z);
	y = cimagf(z);

	x2 = x * x;
	a = 1.0f - x2 - (y * y);

	t = 0.5f * atan2f(2.0f * x, a);
	w = t;

	t = y - 1.0f;
	a = x2 + (t * t);

	t = y + 1.0f;
	a = (x2 + (t * t))/a;
	w = CMPLXF(w, 0.25f * logf(a));
	return w;
}
