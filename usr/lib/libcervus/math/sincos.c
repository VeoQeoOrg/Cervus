#define _GNU_SOURCE
#include "libm.h"

void sincos(double x, double *sin, double *cos)
{
	double y[2], s, c;
	uint32_t ix;
	unsigned n;

	GET_HIGH_WORD(ix, x);
	ix &= 0x7fffffff;

	if (ix <= 0x3fe921fb) {
		if (ix < 0x3e46a09e) {
			FORCE_EVAL(ix < 0x00100000 ? x/0x1p120f : x+0x1p120f);
			*sin = x;
			*cos = 1.0;
			return;
		}
		*sin = __sin(x, 0.0, 0);
		*cos = __cos(x, 0.0);
		return;
	}

	if (ix >= 0x7ff00000) {
		*sin = *cos = x - x;
		return;
	}

	n = __rem_pio2(x, y);
	s = __sin(y[0], y[1], 1);
	c = __cos(y[0], y[1]);
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
