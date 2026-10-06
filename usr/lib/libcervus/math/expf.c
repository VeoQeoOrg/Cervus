#include <math.h>
#include <stdint.h>
#include "libm.h"
#include "exp2f_data.h"

#define N (1 << EXP2F_TABLE_BITS)
#define InvLn2N __exp2f_data.invln2_scaled
#define T __exp2f_data.tab
#define C __exp2f_data.poly_scaled

static inline uint32_t top12(float x)
{
	return asuint(x) >> 20;
}

float expf(float x)
{
	uint32_t abstop;
	uint64_t ki, t;
	double_t kd, xd, z, r, r2, y, s;

	xd = (double_t)x;
	abstop = top12(x) & 0x7ff;
	if (predict_false(abstop >= top12(88.0f))) {
		if (asuint(x) == asuint(-INFINITY))
			return 0.0f;
		if (abstop >= top12(INFINITY))
			return x + x;
		if (x > 0x1.62e42ep6f)
			return __math_oflowf(0);
		if (x < -0x1.9fe368p6f)
			return __math_uflowf(0);
	}

	z = InvLn2N * xd;

#if TOINT_INTRINSICS
	kd = roundtoint(z);
	ki = converttoint(z);
#else
# define SHIFT __exp2f_data.shift
	kd = eval_as_double(z + SHIFT);
	ki = asuint64(kd);
	kd -= SHIFT;
#endif
	r = z - kd;

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
