#include "libm.h"

double tanh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	uint32_t w;
	int sign;
	double_t t;

	sign = u.i >> 63;
	u.i &= (uint64_t)-1/2;
	x = u.f;
	w = u.i >> 32;

	if (w > 0x3fe193ea) {
		if (w > 0x40340000) {
			t = 1 - 0/x;
		} else {
			t = expm1(2*x);
			t = 1 - 2/(t+2);
		}
	} else if (w > 0x3fd058ae) {
		t = expm1(2*x);
		t = t/(t+2);
	} else if (w >= 0x00100000) {
		t = expm1(-2*x);
		t = -t/(t+2);
	} else {
		FORCE_EVAL((float)x);
		t = x;
	}
	return sign ? -t : t;
}
