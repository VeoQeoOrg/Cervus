#include "libm.h"

static const float
pi     = 3.1415927410e+00,
pi_lo  = -8.7422776573e-08;

float atan2f(float y, float x)
{
	float z;
	uint32_t m,ix,iy;

	if (isnan(x) || isnan(y))
		return x+y;
	GET_FLOAT_WORD(ix, x);
	GET_FLOAT_WORD(iy, y);
	if (ix == 0x3f800000)
		return atanf(y);
	m = ((iy>>31)&1) | ((ix>>30)&2);
	ix &= 0x7fffffff;
	iy &= 0x7fffffff;

	if (iy == 0) {
		switch (m) {
		case 0:
		case 1: return y;
		case 2: return  pi;
		case 3: return -pi;
		}
	}

	if (ix == 0)
		return m&1 ? -pi/2 : pi/2;

	if (ix == 0x7f800000) {
		if (iy == 0x7f800000) {
			switch (m) {
			case 0: return  pi/4;
			case 1: return -pi/4;
			case 2: return 3*pi/4;
			case 3: return -3*pi/4;
			}
		} else {
			switch (m) {
			case 0: return  0.0f;
			case 1: return -0.0f;
			case 2: return  pi;
			case 3: return -pi;
			}
		}
	}

	if (ix+(26<<23) < iy || iy == 0x7f800000)
		return m&1 ? -pi/2 : pi/2;

	if ((m&2) && iy+(26<<23) < ix)
		z = 0.0;
	else
		z = atanf(fabsf(y/x));
	switch (m) {
	case 0: return z;
	case 1: return -z;
	case 2: return pi - (z-pi_lo);
	default:
		return (z-pi_lo) - pi;
	}
}
