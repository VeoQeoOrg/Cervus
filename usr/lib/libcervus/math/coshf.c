#include "libm.h"

float coshf(float x)
{
	union {float f; uint32_t i;} u = {.f = x};
	uint32_t w;
	float t;

	u.i &= 0x7fffffff;
	x = u.f;
	w = u.i;

	if (w < 0x3f317217) {
		if (w < 0x3f800000 - (12<<23)) {
			FORCE_EVAL(x + 0x1p120f);
			return 1;
		}
		t = expm1f(x);
		return 1 + t*t/(2*(1+t));
	}

	if (w < 0x42b17217) {
		t = expf(x);
		return 0.5f*(t + 1/t);
	}

	t = __expo2f(x, 1.0f);
	return t;
}
