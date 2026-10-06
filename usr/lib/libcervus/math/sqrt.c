#include <stdint.h>
#include <math.h>
#include "libm.h"
#include "sqrt_data.h"

#define FENV_SUPPORT 1

static inline uint32_t mul32(uint32_t a, uint32_t b)
{
	return (uint64_t)a*b >> 32;
}

static inline uint64_t mul64(uint64_t a, uint64_t b)
{
	uint64_t ahi = a>>32;
	uint64_t alo = a&0xffffffff;
	uint64_t bhi = b>>32;
	uint64_t blo = b&0xffffffff;
	return ahi*bhi + (ahi*blo >> 32) + (alo*bhi >> 32);
}

double sqrt(double x)
{
	uint64_t ix, top, m;

	ix = asuint64(x);
	top = ix >> 52;
	if (predict_false(top - 0x001 >= 0x7ff - 0x001)) {
		if (ix * 2 == 0)
			return x;
		if (ix == 0x7ff0000000000000)
			return x;
		if (ix > 0x7ff0000000000000)
			return __math_invalid(x);

		ix = asuint64(x * 0x1p52);
		top = ix >> 52;
		top -= 52;
	}

	int even = top & 1;
	m = (ix << 11) | 0x8000000000000000;
	if (even) m >>= 1;
	top = (top + 0x3ff) >> 1;

	static const uint64_t three = 0xc0000000;
	uint64_t r, s, d, u, i;

	i = (ix >> 46) % 128;
	r = (uint32_t)__rsqrt_tab[i] << 16;

	s = mul32(m>>32, r);

	d = mul32(s, r);
	u = three - d;
	r = mul32(r, u) << 1;

	s = mul32(s, u) << 1;

	d = mul32(s, r);
	u = three - d;
	r = mul32(r, u) << 1;

	r = r << 32;
	s = mul64(m, r);
	d = mul64(s, r);
	u = (three<<32) - d;
	s = mul64(s, u);

	s = (s - 2) >> 9;

	uint64_t d0, d1, d2;
	double y, t;
	d0 = (m << 42) - s*s;
	d1 = s - d0;
	d2 = d1 + s + 1;
	s += d1 >> 63;
	s &= 0x000fffffffffffff;
	s |= top << 52;
	y = asdouble(s);
	if (FENV_SUPPORT) {
		uint64_t tiny = predict_false(d2==0) ? 0 : 0x0010000000000000;
		tiny |= (d1^d2) & 0x8000000000000000;
		t = asdouble(tiny);
		y = eval_as_double(y + t);
	}
	return y;
}
