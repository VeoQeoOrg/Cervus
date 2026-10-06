#include "complex_impl.h"

static const uint32_t k = 1799;
static const double kln2 = 1246.97177782734161156;

static double __frexp_exp(double x, int *expt)
{
	double exp_x;
	uint32_t hx;

	exp_x = exp(x - kln2);
	GET_HIGH_WORD(hx, exp_x);
	*expt = (hx >> 20) - (0x3ff + 1023) + k;
	SET_HIGH_WORD(exp_x, (hx & 0xfffff) | ((0x3ff + 1023) << 20));
	return exp_x;
}

double complex __ldexp_cexp(double complex z, int expt)
{
	double x, y, exp_x, scale1, scale2;
	int ex_expt, half_expt;

	x = creal(z);
	y = cimag(z);
	exp_x = __frexp_exp(x, &ex_expt);
	expt += ex_expt;

	half_expt = expt / 2;
	INSERT_WORDS(scale1, (0x3ff + half_expt) << 20, 0);
	half_expt = expt - half_expt;
	INSERT_WORDS(scale2, (0x3ff + half_expt) << 20, 0);

	return CMPLX(cos(y) * exp_x * scale1 * scale2, sin(y) * exp_x * scale1 * scale2);
}
