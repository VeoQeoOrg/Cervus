#include <math.h>
#include <stdint.h>
#include "libm.h"
#include "exp2f_data.h"
#include "powf_data.h"

#define N (1 << POWF_LOG2_TABLE_BITS)
#define T __powf_log2_data.tab
#define A __powf_log2_data.poly
#define OFF 0x3f330000

static inline double_t log2_inline(uint32_t ix)
{
	double_t z, r, r2, r4, p, q, y, y0, invc, logc;
	uint32_t iz, top, tmp;
	int k, i;

	tmp = ix - OFF;
	i = (tmp >> (23 - POWF_LOG2_TABLE_BITS)) % N;
	top = tmp & 0xff800000;
	iz = ix - top;
	k = (int32_t)top >> (23 - POWF_SCALE_BITS);
	invc = T[i].invc;
	logc = T[i].logc;
	z = (double_t)asfloat(iz);

	r = z * invc - 1;
	y0 = logc + (double_t)k;

	r2 = r * r;
	y = A[0] * r + A[1];
	p = A[2] * r + A[3];
	r4 = r2 * r2;
	q = A[4] * r + y0;
	q = p * r2 + q;
	y = y * r4 + q;
	return y;
}

#undef N
#undef T
#define N (1 << EXP2F_TABLE_BITS)
#define T __exp2f_data.tab
#define SIGN_BIAS (1 << (EXP2F_TABLE_BITS + 11))

static inline float exp2_inline(double_t xd, uint32_t sign_bias)
{
	uint64_t ki, ski, t;
	double_t kd, z, r, r2, y, s;

#if TOINT_INTRINSICS
#define C __exp2f_data.poly_scaled

	kd = roundtoint(xd);
	ki = converttoint(xd);
#else
#define C __exp2f_data.poly
#define SHIFT __exp2f_data.shift_scaled

	kd = eval_as_double(xd + SHIFT);
	ki = asuint64(kd);
	kd -= SHIFT;
#endif
	r = xd - kd;

	t = T[ki % N];
	ski = ki + sign_bias;
	t += ski << (52 - EXP2F_TABLE_BITS);
	s = asdouble(t);
	z = C[0] * r + C[1];
	r2 = r * r;
	y = C[2] * r + 1;
	y = z * r2 + y;
	y = y * s;
	return eval_as_float(y);
}

static inline int checkint(uint32_t iy)
{
	int e = iy >> 23 & 0xff;
	if (e < 0x7f)
		return 0;
	if (e > 0x7f + 23)
		return 2;
	if (iy & ((1 << (0x7f + 23 - e)) - 1))
		return 0;
	if (iy & (1 << (0x7f + 23 - e)))
		return 1;
	return 2;
}

static inline int zeroinfnan(uint32_t ix)
{
	return 2 * ix - 1 >= 2u * 0x7f800000 - 1;
}

float powf(float x, float y)
{
	uint32_t sign_bias = 0;
	uint32_t ix, iy;

	ix = asuint(x);
	iy = asuint(y);
	if (predict_false(ix - 0x00800000 >= 0x7f800000 - 0x00800000 ||
			  zeroinfnan(iy))) {
		if (predict_false(zeroinfnan(iy))) {
			if (2 * iy == 0)
				return issignalingf_inline(x) ? x + y : 1.0f;
			if (ix == 0x3f800000)
				return issignalingf_inline(y) ? x + y : 1.0f;
			if (2 * ix > 2u * 0x7f800000 ||
			    2 * iy > 2u * 0x7f800000)
				return x + y;
			if (2 * ix == 2 * 0x3f800000)
				return 1.0f;
			if ((2 * ix < 2 * 0x3f800000) == !(iy & 0x80000000))
				return 0.0f;
			return y * y;
		}
		if (predict_false(zeroinfnan(ix))) {
			float_t x2 = x * x;
			if (ix & 0x80000000 && checkint(iy) == 1)
				x2 = -x2;

			return iy & 0x80000000 ? fp_barrierf(1 / x2) : x2;
		}

		if (ix & 0x80000000) {
			int yint = checkint(iy);
			if (yint == 0)
				return __math_invalidf(x);
			if (yint == 1)
				sign_bias = SIGN_BIAS;
			ix &= 0x7fffffff;
		}
		if (ix < 0x00800000) {
			ix = asuint(x * 0x1p23f);
			ix &= 0x7fffffff;
			ix -= 23 << 23;
		}
	}
	double_t logx = log2_inline(ix);
	double_t ylogx = y * logx;
	if (predict_false((asuint64(ylogx) >> 47 & 0xffff) >=
			  asuint64(126.0 * POWF_SCALE) >> 47)) {
		if (ylogx > 0x1.fffffffd1d571p+6 * POWF_SCALE)
			return __math_oflowf(sign_bias);
		if (ylogx <= -150.0 * POWF_SCALE)
			return __math_uflowf(sign_bias);
	}
	return exp2_inline(ylogx, sign_bias);
}
