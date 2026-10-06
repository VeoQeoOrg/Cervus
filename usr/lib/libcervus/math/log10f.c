#include <math.h>
#include <stdint.h>

static const float
ivln10hi  =  4.3432617188e-01,
ivln10lo  = -3.1689971365e-05,
log10_2hi =  3.0102920532e-01,
log10_2lo =  7.9034151668e-07,

Lg1 = 0xaaaaaa.0p-24,
Lg2 = 0xccce13.0p-25,
Lg3 = 0x91e9ee.0p-25,
Lg4 = 0xf89e26.0p-26;

float log10f(float x)
{
	union {float f; uint32_t i;} u = {x};
	float_t hfsq,f,s,z,R,w,t1,t2,dk,hi,lo;
	uint32_t ix;
	int k;

	ix = u.i;
	k = 0;
	if (ix < 0x00800000 || ix>>31) {
		if (ix<<1 == 0)
			return -1/(x*x);
		if (ix>>31)
			return (x-x)/0.0f;

		k -= 25;
		x *= 0x1p25f;
		u.f = x;
		ix = u.i;
	} else if (ix >= 0x7f800000) {
		return x;
	} else if (ix == 0x3f800000)
		return 0;

	ix += 0x3f800000 - 0x3f3504f3;
	k += (int)(ix>>23) - 0x7f;
	ix = (ix&0x007fffff) + 0x3f3504f3;
	u.i = ix;
	x = u.f;

	f = x - 1.0f;
	s = f/(2.0f + f);
	z = s*s;
	w = z*z;
	t1= w*(Lg2+w*Lg4);
	t2= z*(Lg1+w*Lg3);
	R = t2 + t1;
	hfsq = 0.5f*f*f;

	hi = f - hfsq;
	u.f = hi;
	u.i &= 0xfffff000;
	hi = u.f;
	lo = f - hi - hfsq + s*(hfsq+R);
	dk = k;
	return dk*log10_2lo + (lo+hi)*ivln10lo + lo*ivln10hi + hi*ivln10hi + dk*log10_2hi;
}
