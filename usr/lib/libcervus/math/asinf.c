#include "libm.h"

static const double
pio2 = 1.570796326794896558e+00;

static const float

pS0 =  1.6666586697e-01,
pS1 = -4.2743422091e-02,
pS2 = -8.6563630030e-03,
qS1 = -7.0662963390e-01;

static float R(float z)
{
	float_t p, q;
	p = z*(pS0+z*(pS1+z*pS2));
	q = 1.0f+z*qS1;
	return p/q;
}

float asinf(float x)
{
	double s;
	float z;
	uint32_t hx,ix;

	GET_FLOAT_WORD(hx, x);
	ix = hx & 0x7fffffff;
	if (ix >= 0x3f800000) {
		if (ix == 0x3f800000)
			return x*pio2 + 0x1p-120f;
		return 0/(x-x);
	}
	if (ix < 0x3f000000) {
		if (ix < 0x39800000 && ix >= 0x00800000)
			return x;
		return x + x*R(x*x);
	}

	z = (1 - fabsf(x))*0.5f;
	s = sqrt(z);
	x = pio2 - 2*(s+s*R(z));
	if (hx >> 31)
		return -x;
	return x;
}
