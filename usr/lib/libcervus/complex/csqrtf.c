#include "complex_impl.h"

#pragma STDC CX_LIMITED_RANGE ON

float complex csqrtf(float complex z)
{
	float a = crealf(z), b = cimagf(z);
	double t;

	if (z == 0)
		return CMPLXF(0, b);
	if (isinf(b))
		return CMPLXF(INFINITY, b);
	if (isnan(a)) {
		t = (b - b) / (b - b);
		return CMPLXF(a, t);
	}
	if (isinf(a)) {
		if (signbit(a))
			return CMPLXF(fabsf(b - b), copysignf(a, b));
		else
			return CMPLXF(a, copysignf(b - b, b));
	}

	if (a >= 0) {
		t = sqrt((a + hypot(a, b)) * 0.5);
		return CMPLXF(t, b / (2.0 * t));
	} else {
		t = sqrt((-a + hypot(a, b)) * 0.5);
		return CMPLXF(fabsf(b) / (2.0 * t), copysignf(t, b));
	}
}
