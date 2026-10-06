#include "libm.h"

static const double invsqrtpi = 5.64189583547756279280e-01;

double jn(int n, double x)
{
	uint32_t ix, lx;
	int nm1, i, sign;
	double a, b, temp;

	EXTRACT_WORDS(ix, lx, x);
	sign = ix>>31;
	ix &= 0x7fffffff;

	if ((ix | (lx|-lx)>>31) > 0x7ff00000)
		return x;

	if (n == 0)
		return j0(x);
	if (n < 0) {
		nm1 = -(n+1);
		x = -x;
		sign ^= 1;
	} else
		nm1 = n-1;
	if (nm1 == 0)
		return j1(x);

	sign &= n;
	x = fabs(x);
	if ((ix|lx) == 0 || ix == 0x7ff00000)
		b = 0.0;
	else if (nm1 < x) {
		if (ix >= 0x52d00000) {
			switch(nm1&3) {
			case 0: temp = -cos(x)+sin(x); break;
			case 1: temp = -cos(x)-sin(x); break;
			case 2: temp =  cos(x)-sin(x); break;
			default:
			case 3: temp =  cos(x)+sin(x); break;
			}
			b = invsqrtpi*temp/sqrt(x);
		} else {
			a = j0(x);
			b = j1(x);
			for (i=0; i<nm1; ) {
				i++;
				temp = b;
				b = b*(2.0*i/x) - a;
				a = temp;
			}
		}
	} else {
		if (ix < 0x3e100000) {
			if (nm1 > 32)
				b = 0.0;
			else {
				temp = x*0.5;
				b = temp;
				a = 1.0;
				for (i=2; i<=nm1+1; i++) {
					a *= (double)i;
					b *= temp;
				}
				b = b/a;
			}
		} else {
			double t,q0,q1,w,h,z,tmp,nf;
			int k;

			nf = nm1 + 1.0;
			w = 2*nf/x;
			h = 2/x;
			z = w+h;
			q0 = w;
			q1 = w*z - 1.0;
			k = 1;
			while (q1 < 1.0e9) {
				k += 1;
				z += h;
				tmp = z*q1 - q0;
				q0 = q1;
				q1 = tmp;
			}
			for (t=0.0, i=k; i>=0; i--)
				t = 1/(2*(i+nf)/x - t);
			a = t;
			b = 1.0;

			tmp = nf*log(fabs(w));
			if (tmp < 7.09782712893383973096e+02) {
				for (i=nm1; i>0; i--) {
					temp = b;
					b = b*(2.0*i)/x - a;
					a = temp;
				}
			} else {
				for (i=nm1; i>0; i--) {
					temp = b;
					b = b*(2.0*i)/x - a;
					a = temp;

					if (b > 0x1p500) {
						a /= b;
						t /= b;
						b  = 1.0;
					}
				}
			}
			z = j0(x);
			w = j1(x);
			if (fabs(z) >= fabs(w))
				b = t*z/b;
			else
				b = t*w/a;
		}
	}
	return sign ? -b : b;
}

double yn(int n, double x)
{
	uint32_t ix, lx, ib;
	int nm1, sign, i;
	double a, b, temp;

	EXTRACT_WORDS(ix, lx, x);
	sign = ix>>31;
	ix &= 0x7fffffff;

	if ((ix | (lx|-lx)>>31) > 0x7ff00000)
		return x;
	if (sign && (ix|lx)!=0)
		return 0/0.0;
	if (ix == 0x7ff00000)
		return 0.0;

	if (n == 0)
		return y0(x);
	if (n < 0) {
		nm1 = -(n+1);
		sign = n&1;
	} else {
		nm1 = n-1;
		sign = 0;
	}
	if (nm1 == 0)
		return sign ? -y1(x) : y1(x);

	if (ix >= 0x52d00000) {
		switch(nm1&3) {
		case 0: temp = -sin(x)-cos(x); break;
		case 1: temp = -sin(x)+cos(x); break;
		case 2: temp =  sin(x)+cos(x); break;
		default:
		case 3: temp =  sin(x)-cos(x); break;
		}
		b = invsqrtpi*temp/sqrt(x);
	} else {
		a = y0(x);
		b = y1(x);

		GET_HIGH_WORD(ib, b);
		for (i=0; i<nm1 && ib!=0xfff00000; ){
			i++;
			temp = b;
			b = (2.0*i/x)*b - a;
			GET_HIGH_WORD(ib, b);
			a = temp;
		}
	}
	return sign ? -b : b;
}
