#include "complex_impl.h"

double complex ctanh(double complex z)
{
	double x, y;
	double t, beta, s, rho, denom;
	uint32_t hx, ix, lx;

	x = creal(z);
	y = cimag(z);

	EXTRACT_WORDS(hx, lx, x);
	ix = hx & 0x7fffffff;

	if (ix >= 0x7ff00000) {
		if ((ix & 0xfffff) | lx)
			return CMPLX(x, (y == 0 ? y : x * y));
		SET_HIGH_WORD(x, hx - 0x40000000);
		return CMPLX(x, copysign(0, isinf(y) ? y : sin(y) * cos(y)));
	}

	if (!isfinite(y))
		return CMPLX(x ? y - y : x, y - y);

	if (ix >= 0x40360000) {
		double exp_mx = exp(-fabs(x));
		return CMPLX(copysign(1, x), 4 * sin(y) * cos(y) * exp_mx * exp_mx);
	}

	t = tan(y);
	beta = 1.0 + t * t;
	s = sinh(x);
	rho = sqrt(1 + s * s);
	denom = 1 + beta * s * s;
	return CMPLX((beta * rho * s) / denom, t / denom);
}
