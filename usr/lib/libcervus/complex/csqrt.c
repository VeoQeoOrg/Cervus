#include "complex_impl.h"

#pragma STDC CX_LIMITED_RANGE ON

#define THRESH  0x1.a827999fcef32p+1022

double complex csqrt(double complex z)
{
	double complex result;
	double a, b;
	double t;
	int scale;

	a = creal(z);
	b = cimag(z);

	if (z == 0)
		return CMPLX(0, b);
	if (isinf(b))
		return CMPLX(INFINITY, b);
	if (isnan(a)) {
		t = (b - b) / (b - b);
		return CMPLX(a, t);
	}
	if (isinf(a)) {
		if (signbit(a))
			return CMPLX(fabs(b - b), copysign(a, b));
		else
			return CMPLX(a, copysign(b - b, b));
	}

	if (fabs(a) >= THRESH || fabs(b) >= THRESH) {
		a *= 0.25;
		b *= 0.25;
		scale = 1;
	} else {
		scale = 0;
	}

	if (a >= 0) {
		t = sqrt((a + hypot(a, b)) * 0.5);
		result = CMPLX(t, b / (2 * t));
	} else {
		t = sqrt((-a + hypot(a, b)) * 0.5);
		result = CMPLX(fabs(b) / (2 * t), copysign(t, b));
	}

	if (scale)
		result *= 2;
	return result;
}
