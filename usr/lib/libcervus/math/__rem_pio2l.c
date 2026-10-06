#include "libm.h"
#if (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384

static const long double toint = 1.5/LDBL_EPSILON;

#if LDBL_MANT_DIG == 64

#define SMALL(u) (((u.i.se & 0x7fffU)<<16 | u.i.m>>48) < ((0x3fff + 25)<<16 | 0x921f>>1 | 0x8000))
#define QUOBITS(x) ((uint32_t)(int32_t)x & 0x7fffffff)
#define ROUND1 22
#define ROUND2 61
#define NX 3
#define NY 2

static const double
pio2_1 =  1.57079632679597125389e+00,
pio2_2 = -1.07463465549783099519e-12,
pio2_3 =  6.36831716351370313614e-25;
static const long double
pio4    =  0x1.921fb54442d1846ap-1L,
invpio2 =  6.36619772367581343076e-01L,
pio2_1t = -1.07463465549719416346e-12L,
pio2_2t =  6.36831716351095013979e-25L,
pio2_3t = -2.75299651904407171810e-37L;
#elif LDBL_MANT_DIG == 113

#define SMALL(u) (((u.i.se & 0x7fffU)<<16 | u.i.top) < ((0x3fff + 45)<<16 | 0x921f))
#define QUOBITS(x) ((uint32_t)(int64_t)x & 0x7fffffff)
#define ROUND1 51
#define ROUND2 119
#define NX 5
#define NY 3
static const long double
pio4    =  0x1.921fb54442d18469898cc51701b8p-1L,
invpio2 =  6.3661977236758134307553505349005747e-01L,
pio2_1  =  1.5707963267948966192292994253909555e+00L,
pio2_1t =  2.0222662487959507323996846200947577e-21L,
pio2_2  =  2.0222662487959507323994779168837751e-21L,
pio2_2t =  2.0670321098263988236496903051604844e-43L,
pio2_3  =  2.0670321098263988236499468110329591e-43L,
pio2_3t = -2.5650587247459238361625433492959285e-65L;
#endif

int __rem_pio2l(long double x, long double *y)
{
	union ldshape u,uz;
	long double z,w,t,r,fn;
	double tx[NX],ty[NY];
	int ex,ey,n,i;

	u.f = x;
	ex = u.i.se & 0x7fff;
	if (SMALL(u)) {
		fn = x*invpio2 + toint - toint;
		n = QUOBITS(fn);
		r = x-fn*pio2_1;
		w = fn*pio2_1t;

		if (predict_false(r - w < -pio4)) {
			n--;
			fn--;
			r = x - fn*pio2_1;
			w = fn*pio2_1t;
		} else if (predict_false(r - w > pio4)) {
			n++;
			fn++;
			r = x - fn*pio2_1;
			w = fn*pio2_1t;
		}
		y[0] = r-w;
		u.f = y[0];
		ey = u.i.se & 0x7fff;
		if (ex - ey > ROUND1) {
			t = r;
			w = fn*pio2_2;
			r = t-w;
			w = fn*pio2_2t-((t-r)-w);
			y[0] = r-w;
			u.f = y[0];
			ey = u.i.se & 0x7fff;
			if (ex - ey > ROUND2) {
				t = r;
				w = fn*pio2_3;
				r = t-w;
				w = fn*pio2_3t-((t-r)-w);
				y[0] = r-w;
			}
		}
		y[1] = (r - y[0]) - w;
		return n;
	}

	if (ex == 0x7fff) {
		y[0] = y[1] = x - x;
		return 0;
	}

	uz.f = x;
	uz.i.se = 0x3fff + 23;
	z = uz.f;
	for (i=0; i < NX - 1; i++) {
		tx[i] = (double)(int32_t)z;
		z     = (z-tx[i])*0x1p24;
	}
	tx[i] = z;
	while (tx[i] == 0)
		i--;
	n = __rem_pio2_large(tx, ty, ex-0x3fff-23, i+1, NY);
	w = ty[1];
	if (NY == 3)
		w += ty[2];
	r = ty[0] + w;

	w -= r - ty[0];
	if (u.i.se >> 15) {
		y[0] = -r;
		y[1] = -w;
		return -n;
	}
	y[0] = r;
	y[1] = w;
	return n;
}
#endif
