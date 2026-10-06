#include <math.h>
#include <stdint.h>
#include "libm.h"
#include "exp_data.h"
#include "pow_data.h"

#define T __pow_log_data.tab
#define A __pow_log_data.poly
#define Ln2hi __pow_log_data.ln2hi
#define Ln2lo __pow_log_data.ln2lo
#define N (1 << POW_LOG_TABLE_BITS)
#define OFF 0x3fe6955500000000

static inline uint32_t top12(double x)
{
	return asuint64(x) >> 52;
}

static inline double_t log_inline(uint64_t ix, double_t *tail)
{
	double_t z, r, y, invc, logc, logctail, kd, hi, t1, t2, lo, lo1, lo2, p;
	uint64_t iz, tmp;
	int k, i;

	tmp = ix - OFF;
	i = (tmp >> (52 - POW_LOG_TABLE_BITS)) % N;
	k = (int64_t)tmp >> 52;
	iz = ix - (tmp & 0xfffULL << 52);
	z = asdouble(iz);
	kd = (double_t)k;

	invc = T[i].invc;
	logc = T[i].logc;
	logctail = T[i].logctail;

#if __FP_FAST_FMA
	r = __builtin_fma(z, invc, -1.0);
#else

	double_t zhi = asdouble((iz + (1ULL << 31)) & (-1ULL << 32));
	double_t zlo = z - zhi;
	double_t rhi = zhi * invc - 1.0;
	double_t rlo = zlo * invc;
	r = rhi + rlo;
#endif

	t1 = kd * Ln2hi + logc;
	t2 = t1 + r;
	lo1 = kd * Ln2lo + logctail;
	lo2 = t1 - t2 + r;

	double_t ar, ar2, ar3, lo3, lo4;
	ar = A[0] * r;
	ar2 = r * ar;
	ar3 = r * ar2;

#if __FP_FAST_FMA
	hi = t2 + ar2;
	lo3 = __builtin_fma(ar, r, -ar2);
	lo4 = t2 - hi + ar2;
#else
	double_t arhi = A[0] * rhi;
	double_t arhi2 = rhi * arhi;
	hi = t2 + arhi2;
	lo3 = rlo * (ar + arhi);
	lo4 = t2 - hi + arhi2;
#endif

	p = (ar3 * (A[1] + r * A[2] +
		    ar2 * (A[3] + r * A[4] + ar2 * (A[5] + r * A[6]))));
	lo = lo1 + lo2 + lo3 + lo4 + p;
	y = hi + lo;
	*tail = hi - y + lo;
	return y;
}

#undef N
#undef T
#define N (1 << EXP_TABLE_BITS)
#define InvLn2N __exp_data.invln2N
#define NegLn2hiN __exp_data.negln2hiN
#define NegLn2loN __exp_data.negln2loN
#define Shift __exp_data.shift
#define T __exp_data.tab
#define C2 __exp_data.poly[5 - EXP_POLY_ORDER]
#define C3 __exp_data.poly[6 - EXP_POLY_ORDER]
#define C4 __exp_data.poly[7 - EXP_POLY_ORDER]
#define C5 __exp_data.poly[8 - EXP_POLY_ORDER]
#define C6 __exp_data.poly[9 - EXP_POLY_ORDER]

static inline double specialcase(double_t tmp, uint64_t sbits, uint64_t ki)
{
	double_t scale, y;

	if ((ki & 0x80000000) == 0) {
		sbits -= 1009ull << 52;
		scale = asdouble(sbits);
		y = 0x1p1009 * (scale + scale * tmp);
		return eval_as_double(y);
	}

	sbits += 1022ull << 52;

	scale = asdouble(sbits);
	y = scale + scale * tmp;
	if (fabs(y) < 1.0) {
		double_t hi, lo, one = 1.0;
		if (y < 0.0)
			one = -1.0;
		lo = scale - y + scale * tmp;
		hi = one + y;
		lo = one - hi + y + lo;
		y = eval_as_double(hi + lo) - one;

		if (y == 0.0)
			y = asdouble(sbits & 0x8000000000000000);

		fp_force_eval(fp_barrier(0x1p-1022) * 0x1p-1022);
	}
	y = 0x1p-1022 * y;
	return eval_as_double(y);
}

#define SIGN_BIAS (0x800 << EXP_TABLE_BITS)

static inline double exp_inline(double_t x, double_t xtail, uint32_t sign_bias)
{
	uint32_t abstop;
	uint64_t ki, idx, top, sbits;

	double_t kd, z, r, r2, scale, tail, tmp;

	abstop = top12(x) & 0x7ff;
	if (predict_false(abstop - top12(0x1p-54) >=
			  top12(512.0) - top12(0x1p-54))) {
		if (abstop - top12(0x1p-54) >= 0x80000000) {
			double_t one = WANT_ROUNDING ? 1.0 + x : 1.0;
			return sign_bias ? -one : one;
		}
		if (abstop >= top12(1024.0)) {
			if (asuint64(x) >> 63)
				return __math_uflow(sign_bias);
			else
				return __math_oflow(sign_bias);
		}

		abstop = 0;
	}

	z = InvLn2N * x;
#if TOINT_INTRINSICS
	kd = roundtoint(z);
	ki = converttoint(z);
#elif EXP_USE_TOINT_NARROW

	kd = eval_as_double(z + Shift);
	ki = asuint64(kd) >> 16;
	kd = (double_t)(int32_t)ki;
#else

	kd = eval_as_double(z + Shift);
	ki = asuint64(kd);
	kd -= Shift;
#endif
	r = x + kd * NegLn2hiN + kd * NegLn2loN;

	r += xtail;

	idx = 2 * (ki % N);
	top = (ki + sign_bias) << (52 - EXP_TABLE_BITS);
	tail = asdouble(T[idx]);

	sbits = T[idx + 1] + top;

	r2 = r * r;

	tmp = tail + r + r2 * (C2 + r * C3) + r2 * r2 * (C4 + r * C5);
	if (predict_false(abstop == 0))
		return specialcase(tmp, sbits, ki);
	scale = asdouble(sbits);

	return eval_as_double(scale + scale * tmp);
}

static inline int checkint(uint64_t iy)
{
	int e = iy >> 52 & 0x7ff;
	if (e < 0x3ff)
		return 0;
	if (e > 0x3ff + 52)
		return 2;
	if (iy & ((1ULL << (0x3ff + 52 - e)) - 1))
		return 0;
	if (iy & (1ULL << (0x3ff + 52 - e)))
		return 1;
	return 2;
}

static inline int zeroinfnan(uint64_t i)
{
	return 2 * i - 1 >= 2 * asuint64(INFINITY) - 1;
}

double pow(double x, double y)
{
	uint32_t sign_bias = 0;
	uint64_t ix, iy;
	uint32_t topx, topy;

	ix = asuint64(x);
	iy = asuint64(y);
	topx = top12(x);
	topy = top12(y);
	if (predict_false(topx - 0x001 >= 0x7ff - 0x001 ||
			  (topy & 0x7ff) - 0x3be >= 0x43e - 0x3be)) {
		if (predict_false(zeroinfnan(iy))) {
			if (2 * iy == 0)
				return issignaling_inline(x) ? x + y : 1.0;
			if (ix == asuint64(1.0))
				return issignaling_inline(y) ? x + y : 1.0;
			if (2 * ix > 2 * asuint64(INFINITY) ||
			    2 * iy > 2 * asuint64(INFINITY))
				return x + y;
			if (2 * ix == 2 * asuint64(1.0))
				return 1.0;
			if ((2 * ix < 2 * asuint64(1.0)) == !(iy >> 63))
				return 0.0;
			return y * y;
		}
		if (predict_false(zeroinfnan(ix))) {
			double_t x2 = x * x;
			if (ix >> 63 && checkint(iy) == 1)
				x2 = -x2;

			return iy >> 63 ? fp_barrier(1 / x2) : x2;
		}

		if (ix >> 63) {
			int yint = checkint(iy);
			if (yint == 0)
				return __math_invalid(x);
			if (yint == 1)
				sign_bias = SIGN_BIAS;
			ix &= 0x7fffffffffffffff;
			topx &= 0x7ff;
		}
		if ((topy & 0x7ff) - 0x3be >= 0x43e - 0x3be) {
			if (ix == asuint64(1.0))
				return 1.0;
			if ((topy & 0x7ff) < 0x3be) {
				if (WANT_ROUNDING)
					return ix > asuint64(1.0) ? 1.0 + y :
								    1.0 - y;
				else
					return 1.0;
			}
			return (ix > asuint64(1.0)) == (topy < 0x800) ?
				       __math_oflow(0) :
				       __math_uflow(0);
		}
		if (topx == 0) {
			ix = asuint64(x * 0x1p52);
			ix &= 0x7fffffffffffffff;
			ix -= 52ULL << 52;
		}
	}

	double_t lo;
	double_t hi = log_inline(ix, &lo);
	double_t ehi, elo;
#if __FP_FAST_FMA
	ehi = y * hi;
	elo = y * lo + __builtin_fma(y, hi, -ehi);
#else
	double_t yhi = asdouble(iy & -1ULL << 27);
	double_t ylo = y - yhi;
	double_t lhi = asdouble(asuint64(hi) & -1ULL << 27);
	double_t llo = hi - lhi + lo;
	ehi = yhi * lhi;
	elo = ylo * lhi + y * llo;
#endif
	return exp_inline(ehi, elo, sign_bias);
}
