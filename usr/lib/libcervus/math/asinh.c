#include "libm.h"

double asinh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	unsigned e = u.i >> 52 & 0x7ff;
	unsigned s = u.i >> 63;

	u.i &= (uint64_t)-1/2;
	x = u.f;

	if (e >= 0x3ff + 26) {
		x = log(x) + 0.693147180559945309417232121458176568;
	} else if (e >= 0x3ff + 1) {
		x = log(2*x + 1/(sqrt(x*x+1)+x));
	} else if (e >= 0x3ff - 26) {
		x = log1p(x + x*x/(sqrt(x*x+1)+1));
	} else {
		FORCE_EVAL(x + 0x1p120f);
	}
	return s ? -x : x;
}
