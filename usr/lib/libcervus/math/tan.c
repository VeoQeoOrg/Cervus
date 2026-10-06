#include "libm.h"

double tan(double x)
{
	double y[2];
	uint32_t ix;
	unsigned n;

	GET_HIGH_WORD(ix, x);
	ix &= 0x7fffffff;

	if (ix <= 0x3fe921fb) {
		if (ix < 0x3e400000) {
			FORCE_EVAL(ix < 0x00100000 ? x/0x1p120f : x+0x1p120f);
			return x;
		}
		return __tan(x, 0.0, 0);
	}

	if (ix >= 0x7ff00000)
		return x - x;

	n = __rem_pio2(x, y);
	return __tan(y[0], y[1], n&1);
}
