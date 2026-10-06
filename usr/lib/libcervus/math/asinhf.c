#include "libm.h"

float asinhf(float x)
{
	union {float f; uint32_t i;} u = {.f = x};
	uint32_t i = u.i & 0x7fffffff;
	unsigned s = u.i >> 31;

	u.i = i;
	x = u.f;

	if (i >= 0x3f800000 + (12<<23)) {
		x = logf(x) + 0.693147180559945309417232121458176568f;
	} else if (i >= 0x3f800000 + (1<<23)) {
		x = logf(2*x + 1/(sqrtf(x*x+1)+x));
	} else if (i >= 0x3f800000 - (12<<23)) {
		x = log1pf(x + x*x/(sqrtf(x*x+1)+1));
	} else {
		FORCE_EVAL(x + 0x1p120f);
	}
	return s ? -x : x;
}
