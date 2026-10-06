#define _GNU_SOURCE
#include "libm.h"

float jnf(int n, float x)
{
	uint32_t ix;
	int nm1, sign, i;
	float a, b, temp;

	GET_FLOAT_WORD(ix, x);
	sign = ix>>31;
	ix &= 0x7fffffff;
	if (ix > 0x7f800000)
		return x;

	if (n == 0)
		return j0f(x);
	if (n < 0) {
		nm1 = -(n+1);
		x = -x;
		sign ^= 1;
	} else
		nm1 = n-1;
	if (nm1 == 0)
		return j1f(x);

	sign &= n;
	x = fabsf(x);
	if (ix == 0 || ix == 0x7f800000)
		b = 0.0f;
	else if (nm1 < x) {
		a = j0f(x);
		b = j1f(x);
		for (i=0; i<nm1; ){
			i++;
			temp = b;
			b = b*(2.0f*i/x) - a;
			a = temp;
		}
	} else {
		if (ix < 0x35800000) {
			if (nm1 > 8)
				nm1 = 8;
			temp = 0.5f * x;
			b = temp;
			a = 1.0f;
			for (i=2; i<=nm1+1; i++) {
				a *= (float)i;
				b *= temp;
			}
			b = b/a;
		} else {
			float t,q0,q1,w,h,z,tmp,nf;
			int k;

			nf = nm1+1.0f;
			w = 2*nf/x;
			h = 2/x;
			z = w+h;
			q0 = w;
			q1 = w*z - 1.0f;
			k = 1;
			while (q1 < 1.0e4f) {
				k += 1;
				z += h;
				tmp = z*q1 - q0;
				q0 = q1;
				q1 = tmp;
			}
			for (t=0.0f, i=k; i>=0; i--)
				t = 1.0f/(2*(i+nf)/x-t);
			a = t;
			b = 1.0f;

			tmp = nf*logf(fabsf(w));
			if (tmp < 88.721679688f) {
				for (i=nm1; i>0; i--) {
					temp = b;
					b = 2.0f*i*b/x - a;
					a = temp;
				}
			} else {
				for (i=nm1; i>0; i--){
					temp = b;
					b = 2.0f*i*b/x - a;
					a = temp;

					if (b > 0x1p60f) {
						a /= b;
						t /= b;
						b = 1.0f;
					}
				}
			}
			z = j0f(x);
			w = j1f(x);
			if (fabsf(z) >= fabsf(w))
				b = t*z/b;
			else
				b = t*w/a;
		}
	}
	return sign ? -b : b;
}

float ynf(int n, float x)
{
	uint32_t ix, ib;
	int nm1, sign, i;
	float a, b, temp;

	GET_FLOAT_WORD(ix, x);
	sign = ix>>31;
	ix &= 0x7fffffff;
	if (ix > 0x7f800000)
		return x;
	if (sign && ix != 0)
		return 0/0.0f;
	if (ix == 0x7f800000)
		return 0.0f;

	if (n == 0)
		return y0f(x);
	if (n < 0) {
		nm1 = -(n+1);
		sign = n&1;
	} else {
		nm1 = n-1;
		sign = 0;
	}
	if (nm1 == 0)
		return sign ? -y1f(x) : y1f(x);

	a = y0f(x);
	b = y1f(x);

	GET_FLOAT_WORD(ib,b);
	for (i = 0; i < nm1 && ib != 0xff800000; ) {
		i++;
		temp = b;
		b = (2.0f*i/x)*b - a;
		GET_FLOAT_WORD(ib, b);
		a = temp;
	}
	return sign ? -b : b;
}
