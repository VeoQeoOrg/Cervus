#include "libm.h"

static const double
c1pio2 = 1*M_PI_2,
c2pio2 = 2*M_PI_2,
c3pio2 = 3*M_PI_2,
c4pio2 = 4*M_PI_2;

float cosf(float x)
{
	double y;
	uint32_t ix;
	unsigned n, sign;

	GET_FLOAT_WORD(ix, x);
	sign = ix >> 31;
	ix &= 0x7fffffff;

	if (ix <= 0x3f490fda) {
		if (ix < 0x39800000) {
			FORCE_EVAL(x + 0x1p120f);
			return 1.0f;
		}
		return __cosdf(x);
	}
	if (ix <= 0x407b53d1) {
		if (ix > 0x4016cbe3)
			return -__cosdf(sign ? x+c2pio2 : x-c2pio2);
		else {
			if (sign)
				return __sindf(x + c1pio2);
			else
				return __sindf(c1pio2 - x);
		}
	}
	if (ix <= 0x40e231d5) {
		if (ix > 0x40afeddf)
			return __cosdf(sign ? x+c4pio2 : x-c4pio2);
		else {
			if (sign)
				return __sindf(-x - c3pio2);
			else
				return __sindf(x - c3pio2);
		}
	}

	if (ix >= 0x7f800000)
		return x-x;

	n = __rem_pio2f(x,&y);
	switch (n&3) {
	case 0: return  __cosdf(y);
	case 1: return  __sindf(-y);
	case 2: return -__cosdf(y);
	default:
		return  __sindf(y);
	}
}
