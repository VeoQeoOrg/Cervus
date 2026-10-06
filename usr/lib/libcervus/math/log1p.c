#include "libm.h"

static const double
ln2_hi = 6.93147180369123816490e-01,
ln2_lo = 1.90821492927058770002e-10,
Lg1 = 6.666666666666735130e-01,
Lg2 = 3.999999999940941908e-01,
Lg3 = 2.857142874366239149e-01,
Lg4 = 2.222219843214978396e-01,
Lg5 = 1.818357216161805012e-01,
Lg6 = 1.531383769920937332e-01,
Lg7 = 1.479819860511658591e-01;

double log1p(double x)
{
	union {double f; uint64_t i;} u = {x};
	double_t hfsq,f,c,s,z,R,w,t1,t2,dk;
	uint32_t hx,hu;
	int k;

	hx = u.i>>32;
	k = 1;
	if (hx < 0x3fda827a || hx>>31) {
		if (hx >= 0xbff00000) {
			if (x == -1)
				return x/0.0;
			return (x-x)/0.0;
		}
		if (hx<<1 < 0x3ca00000<<1) {
			if ((hx&0x7ff00000) == 0)
				FORCE_EVAL((float)x);
			return x;
		}
		if (hx <= 0xbfd2bec4) {
			k = 0;
			c = 0;
			f = x;
		}
	} else if (hx >= 0x7ff00000)
		return x;
	if (k) {
		u.f = 1 + x;
		hu = u.i>>32;
		hu += 0x3ff00000 - 0x3fe6a09e;
		k = (int)(hu>>20) - 0x3ff;

		if (k < 54) {
			c = k >= 2 ? 1-(u.f-x) : x-(u.f-1);
			c /= u.f;
		} else
			c = 0;

		hu = (hu&0x000fffff) + 0x3fe6a09e;
		u.i = (uint64_t)hu<<32 | (u.i&0xffffffff);
		f = u.f - 1;
	}
	hfsq = 0.5*f*f;
	s = f/(2.0+f);
	z = s*s;
	w = z*z;
	t1 = w*(Lg2+w*(Lg4+w*Lg6));
	t2 = z*(Lg1+w*(Lg3+w*(Lg5+w*Lg7)));
	R = t2 + t1;
	dk = k;
	return s*(hfsq+R) + (dk*ln2_lo+c) - hfsq + f + dk*ln2_hi;
}
