#include "libm.h"

static const double atanhi[] = {
  4.63647609000806093515e-01,
  7.85398163397448278999e-01,
  9.82793723247329054082e-01,
  1.57079632679489655800e+00,
};

static const double atanlo[] = {
  2.26987774529616870924e-17,
  3.06161699786838301793e-17,
  1.39033110312309984516e-17,
  6.12323399573676603587e-17,
};

static const double aT[] = {
  3.33333333333329318027e-01,
 -1.99999999998764832476e-01,
  1.42857142725034663711e-01,
 -1.11111104054623557880e-01,
  9.09088713343650656196e-02,
 -7.69187620504482999495e-02,
  6.66107313738753120669e-02,
 -5.83357013379057348645e-02,
  4.97687799461593236017e-02,
 -3.65315727442169155270e-02,
  1.62858201153657823623e-02,
};

double atan(double x)
{
	double_t w,s1,s2,z;
	uint32_t ix,sign;
	int id;

	GET_HIGH_WORD(ix, x);
	sign = ix >> 31;
	ix &= 0x7fffffff;
	if (ix >= 0x44100000) {
		if (isnan(x))
			return x;
		z = atanhi[3] + 0x1p-120f;
		return sign ? -z : z;
	}
	if (ix < 0x3fdc0000) {
		if (ix < 0x3e400000) {
			if (ix < 0x00100000)

				FORCE_EVAL((float)x);
			return x;
		}
		id = -1;
	} else {
		x = fabs(x);
		if (ix < 0x3ff30000) {
			if (ix < 0x3fe60000) {
				id = 0;
				x = (2.0*x-1.0)/(2.0+x);
			} else {
				id = 1;
				x = (x-1.0)/(x+1.0);
			}
		} else {
			if (ix < 0x40038000) {
				id = 2;
				x = (x-1.5)/(1.0+1.5*x);
			} else {
				id = 3;
				x = -1.0/x;
			}
		}
	}

	z = x*x;
	w = z*z;

	s1 = z*(aT[0]+w*(aT[2]+w*(aT[4]+w*(aT[6]+w*(aT[8]+w*aT[10])))));
	s2 = w*(aT[1]+w*(aT[3]+w*(aT[5]+w*(aT[7]+w*aT[9]))));
	if (id < 0)
		return x - x*(s1+s2);
	z = atanhi[id] - (x*(s1+s2) - atanlo[id] - x);
	return sign ? -z : z;
}
