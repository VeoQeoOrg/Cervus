#include "libm.h"

#if FLT_EVAL_METHOD==0 || FLT_EVAL_METHOD==1
#define EPS DBL_EPSILON
#elif FLT_EVAL_METHOD==2
#define EPS LDBL_EPSILON
#endif

static const double
toint   = 1.5/EPS,
pio4    = 0x1.921fb6p-1,
invpio2 = 6.36619772367581382433e-01,
pio2_1  = 1.57079631090164184570e+00,
pio2_1t = 1.58932547735281966916e-08;

int __rem_pio2f(float x, double *y)
{
	union {float f; uint32_t i;} u = {x};
	double tx[1],ty[1];
	double_t fn;
	uint32_t ix;
	int n, sign, e0;

	ix = u.i & 0x7fffffff;

	if (ix < 0x4dc90fdb) {
		fn = (double_t)x*invpio2 + toint - toint;
		n  = (int32_t)fn;
		*y = x - fn*pio2_1 - fn*pio2_1t;

		if (predict_false(*y < -pio4)) {
			n--;
			fn--;
			*y = x - fn*pio2_1 - fn*pio2_1t;
		} else if (predict_false(*y > pio4)) {
			n++;
			fn++;
			*y = x - fn*pio2_1 - fn*pio2_1t;
		}
		return n;
	}
	if(ix>=0x7f800000) {
		*y = x-x;
		return 0;
	}

	sign = u.i>>31;
	e0 = (ix>>23) - (0x7f+23);
	u.i = ix - (e0<<23);
	tx[0] = u.f;
	n  =  __rem_pio2_large(tx,ty,e0,1,0);
	if (sign) {
		*y = -ty[0];
		return -n;
	}
	*y = ty[0];
	return n;
}
