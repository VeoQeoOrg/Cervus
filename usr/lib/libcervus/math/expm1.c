#include "libm.h"

static const double
o_threshold = 7.09782712893383973096e+02,
ln2_hi      = 6.93147180369123816490e-01,
ln2_lo      = 1.90821492927058770002e-10,
invln2      = 1.44269504088896338700e+00,

Q1 = -3.33333333333331316428e-02,
Q2 =  1.58730158725481460165e-03,
Q3 = -7.93650757867487942473e-05,
Q4 =  4.00821782732936239552e-06,
Q5 = -2.01099218183624371326e-07;

double expm1(double x)
{
	double_t y,hi,lo,c,t,e,hxs,hfx,r1,twopk;
	union {double f; uint64_t i;} u = {x};
	uint32_t hx = u.i>>32 & 0x7fffffff;
	int k, sign = u.i>>63;

	if (hx >= 0x4043687A) {
		if (isnan(x))
			return x;
		if (sign)
			return -1;
		if (x > o_threshold) {
			x *= 0x1p1023;
			return x;
		}
	}

	if (hx > 0x3fd62e42) {
		if (hx < 0x3FF0A2B2) {
			if (!sign) {
				hi = x - ln2_hi;
				lo = ln2_lo;
				k =  1;
			} else {
				hi = x + ln2_hi;
				lo = -ln2_lo;
				k = -1;
			}
		} else {
			k  = invln2*x + (sign ? -0.5 : 0.5);
			t  = k;
			hi = x - t*ln2_hi;
			lo = t*ln2_lo;
		}
		x = hi-lo;
		c = (hi-x)-lo;
	} else if (hx < 0x3c900000) {
		if (hx < 0x00100000)
			FORCE_EVAL((float)x);
		return x;
	} else
		k = 0;

	hfx = 0.5*x;
	hxs = x*hfx;
	r1 = 1.0+hxs*(Q1+hxs*(Q2+hxs*(Q3+hxs*(Q4+hxs*Q5))));
	t  = 3.0-r1*hfx;
	e  = hxs*((r1-t)/(6.0 - x*t));
	if (k == 0)
		return x - (x*e-hxs);
	e  = x*(e-c) - c;
	e -= hxs;

	if (k == -1)
		return 0.5*(x-e) - 0.5;
	if (k == 1) {
		if (x < -0.25)
			return -2.0*(e-(x+0.5));
		return 1.0+2.0*(x-e);
	}
	u.i = (uint64_t)(0x3ff + k)<<52;
	twopk = u.f;
	if (k < 0 || k > 56) {
		y = x - e + 1.0;
		if (k == 1024)
			y = y*2.0*0x1p1023;
		else
			y = y*twopk;
		return y - 1.0;
	}
	u.i = (uint64_t)(0x3ff - k)<<52;
	if (k < 20)
		y = (x-e+(1-u.f))*twopk;
	else
		y = (x-(e+u.f)+1)*twopk;
	return y;
}
