#include "libm.h"

float nexttowardf(float x, long double y)
{
	union {float f; uint32_t i;} ux = {x};
	uint32_t e;

	if (isnan(x) || isnan(y))
		return x + y;
	if (x == y)
		return y;
	if (x == 0) {
		ux.i = 1;
		if (signbit(y))
			ux.i |= 0x80000000;
	} else if (x < y) {
		if (signbit(x))
			ux.i--;
		else
			ux.i++;
	} else {
		if (signbit(x))
			ux.i++;
		else
			ux.i--;
	}
	e = ux.i & 0x7f800000;

	if (e == 0x7f800000)
		FORCE_EVAL(x+x);

	if (e == 0)
		FORCE_EVAL(x*x + ux.f*ux.f);
	return ux.f;
}
