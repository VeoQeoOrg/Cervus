#include "complex_impl.h"

static const uint32_t
exp_ovfl  = 0x42b17218,
cexp_ovfl = 0x43400074;

float complex cexpf(float complex z)
{
	float x, y, exp_x;
	uint32_t hx, hy;

	x = crealf(z);
	y = cimagf(z);

	GET_FLOAT_WORD(hy, y);
	hy &= 0x7fffffff;

	if (hy == 0)
		return CMPLXF(expf(x), y);
	GET_FLOAT_WORD(hx, x);

	if ((hx & 0x7fffffff) == 0)
		return CMPLXF(cosf(y), sinf(y));

	if (hy >= 0x7f800000) {
		if ((hx & 0x7fffffff) != 0x7f800000) {
			return CMPLXF(y - y, y - y);
		} else if (hx & 0x80000000) {
			return CMPLXF(0.0, 0.0);
		} else {
			return CMPLXF(x, y - y);
		}
	}

	if (hx >= exp_ovfl && hx <= cexp_ovfl) {
		return __ldexp_cexpf(z, 0);
	} else {
		exp_x = expf(x);
		return CMPLXF(exp_x * cosf(y), exp_x * sinf(y));
	}
}
