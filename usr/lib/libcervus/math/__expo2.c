#include "libm.h"

static const int k = 2043;
static const double kln2 = 0x1.62066151add8bp+10;

double __expo2(double x, double sign)
{
	double scale;

	INSERT_WORDS(scale, (uint32_t)(0x3ff + k/2) << 20, 0);

	return exp(x - kln2) * (sign * scale) * scale;
}
