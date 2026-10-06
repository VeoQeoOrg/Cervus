#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double cbrtl(long double x)
{
	return cbrt(x);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
static const unsigned B1 = 709958130;

long double cbrtl(long double x)
{
	union ldshape u = {x}, v;
	union {float f; uint32_t i;} uft;
	long double r, s, t, w;
	double_t dr, dt, dx;
	float_t ft;
	int e = u.i.se & 0x7fff;
	int sign = u.i.se & 0x8000;

	if (e == 0x7fff)
		return x + x;
	if (e == 0) {
		u.f *= 0x1p120;
		e = u.i.se & 0x7fff;

		if (e == 0)
			return x;
		e -= 120;
	}
	e -= 0x3fff;
	u.i.se = 0x3fff;
	x = u.f;
	switch (e % 3) {
	case 1:
	case -2:
		x *= 2;
		e--;
		break;
	case 2:
	case -1:
		x *= 4;
		e -= 2;
		break;
	}
	v.f = 1.0;
	v.i.se = sign | (0x3fff + e/3);

	uft.f = x;
	uft.i = (uft.i & 0x7fffffff)/3 + B1;
	ft = uft.f;

	dx = x;
	dt = ft;
	dr = dt * dt * dt;
	dt = dt * (dx + dx + dr) / (dx + dr + dr);

	dr = dt * dt * dt;
	dt = dt * (dx + dx + dr) / (dx + dr + dr);

#if LDBL_MANT_DIG == 64

	t = dt + (0x1.0p32L + 0x1.0p-31L) - 0x1.0p32;
#elif LDBL_MANT_DIG == 113

	t = dt + 0x2.0p-46 + 0x1.0p60L - 0x1.0p60;
#endif

	s = t*t;
	r = x/s;
	w = t+t;
	r = (r-t)/(w+r);
	t = t+t*r;

	t *= v.f;
	return t;
}
#endif
