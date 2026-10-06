#include "libm.h"

#if FLT_EVAL_METHOD==2
#undef sqrt
#define sqrt sqrtl
#endif

double acosh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	unsigned e = u.i >> 52 & 0x7ff;

	if (e < 0x3ff + 1)

		return log1p(x-1 + sqrt((x-1)*(x-1)+2*(x-1)));
	if (e < 0x3ff + 26)

		return log(2*x - 1/(x+sqrt(x*x-1)));

	return log(x) + 0.693147180559945309417232121458176568;
}
