#include "libm.h"

static const double
s1pio2 = 1*M_PI_2,
s2pio2 = 2*M_PI_2,
s3pio2 = 3*M_PI_2,
s4pio2 = 4*M_PI_2;

float sinf(float x)
{
	double y;
	uint32_t ix;
	int n, sign;

	GET_FLOAT_WORD(ix, x);
	sign = ix >> 31;
	ix &= 0x7fffffff;

	if (ix <= 0x3f490fda) {
		if (ix < 0x39800000) {
			FORCE_EVAL(ix < 0x00800000 ? x/0x1p120f : x+0x1p120f);
			return x;
		}
		return __sindf(x);
	}
	if (ix <= 0x407b53d1) {
		if (ix <= 0x4016cbe3) {
			if (sign)
				return -__cosdf(x + s1pio2);
			else
				return __cosdf(x - s1pio2);
		}
		return __sindf(sign ? -(x + s2pio2) : -(x - s2pio2));
	}
	if (ix <= 0x40e231d5) {
		if (ix <= 0x40afeddf) {
			if (sign)
				return __cosdf(x + s3pio2);
			else
				return -__cosdf(x - s3pio2);
		}
		return __sindf(sign ? x + s4pio2 : x - s4pio2);
	}

	if (ix >= 0x7f800000)
		return x - x;

	n = __rem_pio2f(x, &y);
	switch (n&3) {
	case 0: return  __sindf(y);
	case 1: return  __cosdf(y);
	case 2: return  __sindf(-y);
	default:
		return -__cosdf(y);
	}
}
