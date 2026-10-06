#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double atan2l(long double y, long double x)
{
	return atan2(y, x);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
#include "__invtrigl.h"

long double atan2l(long double y, long double x)
{
	union ldshape ux, uy;
	long double z;
	int m, ex, ey;

	if (isnan(x) || isnan(y))
		return x+y;
	if (x == 1)
		return atanl(y);
	ux.f = x;
	uy.f = y;
	ex = ux.i.se & 0x7fff;
	ey = uy.i.se & 0x7fff;
	m = 2*(ux.i.se>>15) | uy.i.se>>15;
	if (y == 0) {
		switch(m) {
		case 0:
		case 1: return y;
		case 2: return  2*pio2_hi;
		case 3: return -2*pio2_hi;
		}
	}
	if (x == 0)
		return m&1 ? -pio2_hi : pio2_hi;
	if (ex == 0x7fff) {
		if (ey == 0x7fff) {
			switch(m) {
			case 0: return  pio2_hi/2;
			case 1: return -pio2_hi/2;
			case 2: return  1.5*pio2_hi;
			case 3: return -1.5*pio2_hi;
			}
		} else {
			switch(m) {
			case 0: return  0.0;
			case 1: return -0.0;
			case 2: return  2*pio2_hi;
			case 3: return -2*pio2_hi;
			}
		}
	}
	if (ex+120 < ey || ey == 0x7fff)
		return m&1 ? -pio2_hi : pio2_hi;

	if ((m&2) && ey+120 < ex)
		z = 0.0;
	else
		z = atanl(fabsl(y/x));
	switch (m) {
	case 0: return z;
	case 1: return -z;
	case 2: return 2*pio2_hi-(z-2*pio2_lo);
	default:
		return (z-2*pio2_lo)-2*pio2_hi;
	}
}
#endif
