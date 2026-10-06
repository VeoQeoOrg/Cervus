#define _GNU_SOURCE
#include "libm.h"

static const double
s1pio2 = 1*M_PI_2,
s2pio2 = 2*M_PI_2,
s3pio2 = 3*M_PI_2,
s4pio2 = 4*M_PI_2;

void sincosf(float x, float *sin, float *cos)
{
	double y;
	float_t s, c;
	uint32_t ix;
	unsigned n, sign;

	GET_FLOAT_WORD(ix, x);
	sign = ix >> 31;
	ix &= 0x7fffffff;

	if (ix <= 0x3f490fda) {
		if (ix < 0x39800000) {
			FORCE_EVAL(ix < 0x00100000 ? x/0x1p120f : x+0x1p120f);
			*sin = x;
			*cos = 1.0f;
			return;
		}
		*sin = __sindf(x);
		*cos = __cosdf(x);
		return;
	}

	if (ix <= 0x407b53d1) {
		if (ix <= 0x4016cbe3) {
			if (sign) {
				*sin = -__cosdf(x + s1pio2);
				*cos = __sindf(x + s1pio2);
			} else {
				*sin = __cosdf(s1pio2 - x);
				*cos = __sindf(s1pio2 - x);
			}
			return;
		}

		*sin = -__sindf(sign ? x + s2pio2 : x - s2pio2);
		*cos = -__cosdf(sign ? x + s2pio2 : x - s2pio2);
		return;
	}

	if (ix <= 0x40e231d5) {
		if (ix <= 0x40afeddf) {
			if (sign) {
				*sin = __cosdf(x + s3pio2);
				*cos = -__sindf(x + s3pio2);
			} else {
				*sin = -__cosdf(x - s3pio2);
				*cos = __sindf(x - s3pio2);
			}
			return;
		}
		*sin = __sindf(sign ? x + s4pio2 : x - s4pio2);
		*cos = __cosdf(sign ? x + s4pio2 : x - s4pio2);
		return;
	}

	if (ix >= 0x7f800000) {
		*sin = *cos = x - x;
		return;
	}

	n = __rem_pio2f(x, &y);
	s = __sindf(y);
	c = __cosdf(y);
	switch (n&3) {
	case 0:
		*sin = s;
		*cos = c;
		break;
	case 1:
		*sin = c;
		*cos = -s;
		break;
	case 2:
		*sin = -s;
		*cos = -c;
		break;
	case 3:
	default:
		*sin = -c;
		*cos = s;
		break;
	}
}
