#include "libm.h"

double sinh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	uint32_t w;
	double t, h, absx;

	h = 0.5;
	if (u.i >> 63)
		h = -h;

	u.i &= (uint64_t)-1/2;
	absx = u.f;
	w = u.i >> 32;

	if (w < 0x40862e42) {
		t = expm1(absx);
		if (w < 0x3ff00000) {
			if (w < 0x3ff00000 - (26<<20))

				return x;
			return h*(2*t - t*t/(t+1));
		}

		return h*(t + t/(t+1));
	}

	t = __expo2(absx, 2*h);
	return t;
}
