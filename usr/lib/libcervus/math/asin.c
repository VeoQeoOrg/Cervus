#include "libm.h"

static const double
pio2_hi = 1.57079632679489655800e+00,
pio2_lo = 6.12323399573676603587e-17,

pS0 =  1.66666666666666657415e-01,
pS1 = -3.25565818622400915405e-01,
pS2 =  2.01212532134862925881e-01,
pS3 = -4.00555345006794114027e-02,
pS4 =  7.91534994289814532176e-04,
pS5 =  3.47933107596021167570e-05,
qS1 = -2.40339491173441421878e+00,
qS2 =  2.02094576023350569471e+00,
qS3 = -6.88283971605453293030e-01,
qS4 =  7.70381505559019352791e-02;

static double R(double z)
{
	double_t p, q;
	p = z*(pS0+z*(pS1+z*(pS2+z*(pS3+z*(pS4+z*pS5)))));
	q = 1.0+z*(qS1+z*(qS2+z*(qS3+z*qS4)));
	return p/q;
}

double asin(double x)
{
	double z,r,s;
	uint32_t hx,ix;

	GET_HIGH_WORD(hx, x);
	ix = hx & 0x7fffffff;

	if (ix >= 0x3ff00000) {
		uint32_t lx;
		GET_LOW_WORD(lx, x);
		if ((ix-0x3ff00000 | lx) == 0)

			return x*pio2_hi + 0x1p-120f;
		return 0/(x-x);
	}

	if (ix < 0x3fe00000) {
		if (ix < 0x3e500000 && ix >= 0x00100000)
			return x;
		return x + x*R(x*x);
	}

	z = (1 - fabs(x))*0.5;
	s = sqrt(z);
	r = R(z);
	if (ix >= 0x3fef3333) {
		x = pio2_hi-(2*(s+s*r)-pio2_lo);
	} else {
		double f,c;

		f = s;
		SET_LOW_WORD(f,0);
		c = (z-f*f)/(s+f);
		x = 0.5*pio2_hi - (2*s*r - (pio2_lo-2*c) - (0.5*pio2_hi-2*f));
	}
	if (hx >> 31)
		return -x;
	return x;
}
