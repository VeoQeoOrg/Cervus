#include "libm.h"

static const float
pi  =  3.1415927410e+00,
a0  =  7.7215664089e-02,
a1  =  3.2246702909e-01,
a2  =  6.7352302372e-02,
a3  =  2.0580807701e-02,
a4  =  7.3855509982e-03,
a5  =  2.8905137442e-03,
a6  =  1.1927076848e-03,
a7  =  5.1006977446e-04,
a8  =  2.2086278477e-04,
a9  =  1.0801156895e-04,
a10 =  2.5214456400e-05,
a11 =  4.4864096708e-05,
tc  =  1.4616321325e+00,
tf  = -1.2148628384e-01,

tt  =  6.6971006518e-09,
t0  =  4.8383611441e-01,
t1  = -1.4758771658e-01,
t2  =  6.4624942839e-02,
t3  = -3.2788541168e-02,
t4  =  1.7970675603e-02,
t5  = -1.0314224288e-02,
t6  =  6.1005386524e-03,
t7  = -3.6845202558e-03,
t8  =  2.2596477065e-03,
t9  = -1.4034647029e-03,
t10 =  8.8108185446e-04,
t11 = -5.3859531181e-04,
t12 =  3.1563205994e-04,
t13 = -3.1275415677e-04,
t14 =  3.3552918467e-04,
u0  = -7.7215664089e-02,
u1  =  6.3282704353e-01,
u2  =  1.4549225569e+00,
u3  =  9.7771751881e-01,
u4  =  2.2896373272e-01,
u5  =  1.3381091878e-02,
v1  =  2.4559779167e+00,
v2  =  2.1284897327e+00,
v3  =  7.6928514242e-01,
v4  =  1.0422264785e-01,
v5  =  3.2170924824e-03,
s0  = -7.7215664089e-02,
s1  =  2.1498242021e-01,
s2  =  3.2577878237e-01,
s3  =  1.4635047317e-01,
s4  =  2.6642270386e-02,
s5  =  1.8402845599e-03,
s6  =  3.1947532989e-05,
r1  =  1.3920053244e+00,
r2  =  7.2193557024e-01,
r3  =  1.7193385959e-01,
r4  =  1.8645919859e-02,
r5  =  7.7794247773e-04,
r6  =  7.3266842264e-06,
w0  =  4.1893854737e-01,
w1  =  8.3333335817e-02,
w2  = -2.7777778450e-03,
w3  =  7.9365057172e-04,
w4  = -5.9518753551e-04,
w5  =  8.3633989561e-04,
w6  = -1.6309292987e-03;

static float sin_pi(float x)
{
	double_t y;
	int n;

	x = 2*(x*0.5f - floorf(x*0.5f));

	n = (int)(x*4);
	n = (n+1)/2;
	y = x - n*0.5f;
	y *= 3.14159265358979323846;
	switch (n) {
	default:
	case 0: return __sindf(y);
	case 1: return __cosdf(y);
	case 2: return __sindf(-y);
	case 3: return -__cosdf(y);
	}
}

float __lgammaf_r(float x, int *signgamp)
{
	union {float f; uint32_t i;} u = {x};
	float t,y,z,nadj,p,p1,p2,p3,q,r,w;
	uint32_t ix;
	int i,sign;

	*signgamp = 1;
	sign = u.i>>31;
	ix = u.i & 0x7fffffff;
	if (ix >= 0x7f800000)
		return x*x;
	if (ix < 0x35000000) {
		if (sign) {
			*signgamp = -1;
			x = -x;
		}
		return -logf(x);
	}
	if (sign) {
		x = -x;
		t = sin_pi(x);
		if (t == 0.0f)
			return 1.0f/(x-x);
		if (t > 0.0f)
			*signgamp = -1;
		else
			t = -t;
		nadj = logf(pi/(t*x));
	}

	if (ix == 0x3f800000 || ix == 0x40000000)
		r = 0;

	else if (ix < 0x40000000) {
		if (ix <= 0x3f666666) {
			r = -logf(x);
			if (ix >= 0x3f3b4a20) {
				y = 1.0f - x;
				i = 0;
			} else if (ix >= 0x3e6d3308) {
				y = x - (tc-1.0f);
				i = 1;
			} else {
				y = x;
				i = 2;
			}
		} else {
			r = 0.0f;
			if (ix >= 0x3fdda618) {
				y = 2.0f - x;
				i = 0;
			} else if (ix >= 0x3F9da620) {
				y = x - tc;
				i = 1;
			} else {
				y = x - 1.0f;
				i = 2;
			}
		}
		switch(i) {
		case 0:
			z = y*y;
			p1 = a0+z*(a2+z*(a4+z*(a6+z*(a8+z*a10))));
			p2 = z*(a1+z*(a3+z*(a5+z*(a7+z*(a9+z*a11)))));
			p = y*p1+p2;
			r += p - 0.5f*y;
			break;
		case 1:
			z = y*y;
			w = z*y;
			p1 = t0+w*(t3+w*(t6+w*(t9 +w*t12)));
			p2 = t1+w*(t4+w*(t7+w*(t10+w*t13)));
			p3 = t2+w*(t5+w*(t8+w*(t11+w*t14)));
			p = z*p1-(tt-w*(p2+y*p3));
			r += (tf + p);
			break;
		case 2:
			p1 = y*(u0+y*(u1+y*(u2+y*(u3+y*(u4+y*u5)))));
			p2 = 1.0f+y*(v1+y*(v2+y*(v3+y*(v4+y*v5))));
			r += -0.5f*y + p1/p2;
		}
	} else if (ix < 0x41000000) {
		i = (int)x;
		y = x - (float)i;
		p = y*(s0+y*(s1+y*(s2+y*(s3+y*(s4+y*(s5+y*s6))))));
		q = 1.0f+y*(r1+y*(r2+y*(r3+y*(r4+y*(r5+y*r6)))));
		r = 0.5f*y+p/q;
		z = 1.0f;
		switch (i) {
		case 7: z *= y + 6.0f;
		case 6: z *= y + 5.0f;
		case 5: z *= y + 4.0f;
		case 4: z *= y + 3.0f;
		case 3: z *= y + 2.0f;
			r += logf(z);
			break;
		}
	} else if (ix < 0x5c800000) {
		t = logf(x);
		z = 1.0f/x;
		y = z*z;
		w = w0+z*(w1+y*(w2+y*(w3+y*(w4+y*(w5+y*w6)))));
		r = (x-0.5f)*(t-1.0f)+w;
	} else
		r =  x*(logf(x)-1.0f);
	if (sign)
		r = nadj - r;
	return r;
}

weak_alias(__lgammaf_r, lgammaf_r);
