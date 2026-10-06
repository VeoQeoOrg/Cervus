#include <math.h>
#include <stdint.h>
#include "libm.h"
#include "log2f_data.h"

#define N (1 << LOG2F_TABLE_BITS)
#define T __log2f_data.tab
#define A __log2f_data.poly
#define OFF 0x3f330000

float log2f(float x)
{
	double_t z, r, r2, p, y, y0, invc, logc;
	uint32_t ix, iz, top, tmp;
	int k, i;

	ix = asuint(x);

	if (WANT_ROUNDING && predict_false(ix == 0x3f800000))
		return 0;
	if (predict_false(ix - 0x00800000 >= 0x7f800000 - 0x00800000)) {
		if (ix * 2 == 0)
			return __math_divzerof(1);
		if (ix == 0x7f800000)
			return x;
		if ((ix & 0x80000000) || ix * 2 >= 0xff000000)
			return __math_invalidf(x);

		ix = asuint(x * 0x1p23f);
		ix -= 23 << 23;
	}

	tmp = ix - OFF;
	i = (tmp >> (23 - LOG2F_TABLE_BITS)) % N;
	top = tmp & 0xff800000;
	iz = ix - top;
	k = (int32_t)tmp >> 23;
	invc = T[i].invc;
	logc = T[i].logc;
	z = (double_t)asfloat(iz);

	r = z * invc - 1;
	y0 = logc + (double_t)k;

	r2 = r * r;
	y = A[1] * r + A[2];
	y = A[0] * r2 + y;
	p = A[3] * r + y0;
	y = y * r2 + p;
	return eval_as_float(y);
}
