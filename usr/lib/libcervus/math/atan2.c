#include "libm.h"

static const double
pi     = 3.1415926535897931160E+00,
pi_lo  = 1.2246467991473531772E-16;

double atan2(double y, double x)
{
	double z;
	uint32_t m,lx,ly,ix,iy;

	if (isnan(x) || isnan(y))
		return x+y;
	EXTRACT_WORDS(ix, lx, x);
	EXTRACT_WORDS(iy, ly, y);
	if ((ix-0x3ff00000 | lx) == 0)
		return atan(y);
	m = ((iy>>31)&1) | ((ix>>30)&2);
	ix = ix & 0x7fffffff;
	iy = iy & 0x7fffffff;

	if ((iy|ly) == 0) {
		switch(m) {
		case 0:
		case 1: return y;
		case 2: return  pi;
		case 3: return -pi;
		}
	}

	if ((ix|lx) == 0)
		return m&1 ? -pi/2 : pi/2;

	if (ix == 0x7ff00000) {
		if (iy == 0x7ff00000) {
			switch(m) {
			case 0: return  pi/4;
			case 1: return -pi/4;
			case 2: return  3*pi/4;
			case 3: return -3*pi/4;
			}
		} else {
			switch(m) {
			case 0: return  0.0;
			case 1: return -0.0;
			case 2: return  pi;
			case 3: return -pi;
			}
		}
	}

	if (ix+(64<<20) < iy || iy == 0x7ff00000)
		return m&1 ? -pi/2 : pi/2;

	if ((m&2) && iy+(64<<20) < ix)
		z = 0;
	else
		z = atan(fabs(y/x));
	switch (m) {
	case 0: return z;
	case 1: return -z;
	case 2: return pi - (z-pi_lo);
	default:
		return (z-pi_lo) - pi;
	}
}
