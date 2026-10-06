#include "complex_impl.h"

static const float float_pi_2 = M_PI_2;

float complex cacosf(float complex z)
{
	z = casinf(z);
	return CMPLXF(float_pi_2 - crealf(z), -cimagf(z));
}
