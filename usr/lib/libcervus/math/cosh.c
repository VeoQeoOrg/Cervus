#include "libm.h"

double cosh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	uint32_t w;
	double t;

	u.i &= (uint64_t)-1/2;
	x = u.f;
	w = u.i >> 32;

	if (w < 0x3fe62e42) {
		if (w < 0x3ff00000 - (26<<20)) {
			FORCE_EVAL(x + 0x1p120f);
			return 1;
		}
		t = expm1(x);
		return 1 + t*t/(2*(1+t));
	}

	if (w < 0x40862e42) {
		t = exp(x);

		return 0.5*(t + 1/t);
	}

	t = __expo2(x, 1.0);
	return t;
}
