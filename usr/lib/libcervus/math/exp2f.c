#include <math.h>
#include <stdint.h>
#include "libm.h"
#include "exp2f_data.h"

#define N (1 << EXP2F_TABLE_BITS)
#define T __exp2f_data.tab
#define C __exp2f_data.poly
#define SHIFT __exp2f_data.shift_scaled

static inline uint32_t top12(float x)
{
	return asuint(x) >> 20;
}

float exp2f(float x)
{
	uint32_t abstop;
	uint64_t ki, t;
	double_t kd, xd, z, r, r2, y, s;

	xd = (double_t)x;
	abstop = top12(x) & 0x7ff;
	if (predict_false(abstop >= top12(128.0f))) {
		if (asuint(x) == asuint(-INFINITY))
			return 0.0f;
		if (abstop >= top12(INFINITY))
			return x + x;
		if (x > 0.0f)
			return __math_oflowf(0);
		if (x <= -150.0f)
			return __math_uflowf(0);
	}

	kd = eval_as_double(xd + SHIFT);
	ki = asuint64(kd);
	kd -= SHIFT;
	r = xd - kd;

	t = T[ki % N];
	t += ki << (52 - EXP2F_TABLE_BITS);
	s = asdouble(t);
	z = C[0] * r + C[1];
	r2 = r * r;
	y = C[2] * r + 1;
	y = z * r2 + y;
	y = y * s;
	return eval_as_float(y);
}
