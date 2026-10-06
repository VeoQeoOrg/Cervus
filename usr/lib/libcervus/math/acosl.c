#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double acosl(long double x)
{
	return acos(x);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
#include "__invtrigl.h"
#if LDBL_MANT_DIG == 64
#define CLEARBOTTOM(u) (u.i.m &= -1ULL << 32)
#elif LDBL_MANT_DIG == 113
#define CLEARBOTTOM(u) (u.i.lo = 0)
#endif

long double acosl(long double x)
{
	union ldshape u = {x};
	long double z, s, c, f;
	uint16_t e = u.i.se & 0x7fff;

	if (e >= 0x3fff) {
		if (x == 1)
			return 0;
		if (x == -1)
			return 2*pio2_hi + 0x1p-120f;
		return 0/(x-x);
	}

	if (e < 0x3fff - 1) {
		if (e < 0x3fff - LDBL_MANT_DIG - 1)
			return pio2_hi + 0x1p-120f;
		return pio2_hi - (__invtrigl_R(x*x)*x - pio2_lo + x);
	}

	if (u.i.se >> 15) {
		z = (1 + x)*0.5;
		s = sqrtl(z);
		return 2*(pio2_hi - (__invtrigl_R(z)*s - pio2_lo + s));
	}

	z = (1 - x)*0.5;
	s = sqrtl(z);
	u.f = s;
	CLEARBOTTOM(u);
	f = u.f;
	c = (z - f*f)/(s + f);
	return 2*(__invtrigl_R(z)*s + c + f);
}
#endif
