#include "libm.h"

float tanhf(float x)
{
	union {float f; uint32_t i;} u = {.f = x};
	uint32_t w;
	int sign;
	float t;

	sign = u.i >> 31;
	u.i &= 0x7fffffff;
	x = u.f;
	w = u.i;

	if (w > 0x3f0c9f54) {
		if (w > 0x41200000) {
			t = 1 + 0/x;
		} else {
			t = expm1f(2*x);
			t = 1 - 2/(t+2);
		}
	} else if (w > 0x3e82c578) {
		t = expm1f(2*x);
		t = t/(t+2);
	} else if (w >= 0x00800000) {
		t = expm1f(-2*x);
		t = -t/(t+2);
	} else {
		FORCE_EVAL(x*x);
		t = x;
	}
	return sign ? -t : t;
}
