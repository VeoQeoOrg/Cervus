#include "libm.h"

static const double
t1pio2 = 1*M_PI_2,
t2pio2 = 2*M_PI_2,
t3pio2 = 3*M_PI_2,
t4pio2 = 4*M_PI_2;

float tanf(float x)
{
	double y;
	uint32_t ix;
	unsigned n, sign;

	GET_FLOAT_WORD(ix, x);
	sign = ix >> 31;
	ix &= 0x7fffffff;

	if (ix <= 0x3f490fda) {
		if (ix < 0x39800000) {
			FORCE_EVAL(ix < 0x00800000 ? x/0x1p120f : x+0x1p120f);
			return x;
		}
		return __tandf(x, 0);
	}
	if (ix <= 0x407b53d1) {
		if (ix <= 0x4016cbe3)
			return __tandf((sign ? x+t1pio2 : x-t1pio2), 1);
		else
			return __tandf((sign ? x+t2pio2 : x-t2pio2), 0);
	}
	if (ix <= 0x40e231d5) {
		if (ix <= 0x40afeddf)
			return __tandf((sign ? x+t3pio2 : x-t3pio2), 1);
		else
			return __tandf((sign ? x+t4pio2 : x-t4pio2), 0);
	}

	if (ix >= 0x7f800000)
		return x - x;

	n = __rem_pio2f(x, &y);
	return __tandf(y, n&1);
}
