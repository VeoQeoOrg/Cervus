#include "libm.h"

double atanh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	unsigned e = u.i >> 52 & 0x7ff;
	unsigned s = u.i >> 63;
	double_t y;

	u.i &= (uint64_t)-1/2;
	y = u.f;

	if (e < 0x3ff - 1) {
		if (e < 0x3ff - 32) {
			if (e == 0)
				FORCE_EVAL((float)y);
		} else {
			y = 0.5*log1p(2*y + 2*y*y/(1-y));
		}
	} else {
		y = 0.5*log1p(2*(y/(1-y)));
	}
	return s ? -y : y;
}
