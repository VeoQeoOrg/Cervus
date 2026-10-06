#include "complex_impl.h"

static const uint32_t
exp_ovfl  = 0x40862e42,
cexp_ovfl = 0x4096b8e4;

double complex cexp(double complex z)
{
	double x, y, exp_x;
	uint32_t hx, hy, lx, ly;

	x = creal(z);
	y = cimag(z);

	EXTRACT_WORDS(hy, ly, y);
	hy &= 0x7fffffff;

	if ((hy | ly) == 0)
		return CMPLX(exp(x), y);
	EXTRACT_WORDS(hx, lx, x);

	if (((hx & 0x7fffffff) | lx) == 0)
		return CMPLX(cos(y), sin(y));

	if (hy >= 0x7ff00000) {
		if (lx != 0 || (hx & 0x7fffffff) != 0x7ff00000) {
			return CMPLX(y - y, y - y);
		} else if (hx & 0x80000000) {
			return CMPLX(0.0, 0.0);
		} else {
			return CMPLX(x, y - y);
		}
	}

	if (hx >= exp_ovfl && hx <= cexp_ovfl) {
		return __ldexp_cexp(z, 0);
	} else {
		exp_x = exp(x);
		return CMPLX(exp_x * cos(y), exp_x * sin(y));
	}
}
