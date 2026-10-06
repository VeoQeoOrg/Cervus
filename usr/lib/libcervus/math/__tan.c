#include "libm.h"

static const double T[] = {
             3.33333333333334091986e-01,
             1.33333333333201242699e-01,
             5.39682539762260521377e-02,
             2.18694882948595424599e-02,
             8.86323982359930005737e-03,
             3.59207910759131235356e-03,
             1.45620945432529025516e-03,
             5.88041240820264096874e-04,
             2.46463134818469906812e-04,
             7.81794442939557092300e-05,
             7.14072491382608190305e-05,
            -1.85586374855275456654e-05,
             2.59073051863633712884e-05,
},
pio4 =       7.85398163397448278999e-01,
pio4lo =     3.06161699786838301793e-17;

double __tan(double x, double y, int odd)
{
	double_t z, r, v, w, s, a;
	double w0, a0;
	uint32_t hx;
	int big, sign;

	GET_HIGH_WORD(hx,x);
	big = (hx&0x7fffffff) >= 0x3FE59428;
	if (big) {
		sign = hx>>31;
		if (sign) {
			x = -x;
			y = -y;
		}
		x = (pio4 - x) + (pio4lo - y);
		y = 0.0;
	}
	z = x * x;
	w = z * z;

	r = T[1] + w*(T[3] + w*(T[5] + w*(T[7] + w*(T[9] + w*T[11]))));
	v = z*(T[2] + w*(T[4] + w*(T[6] + w*(T[8] + w*(T[10] + w*T[12])))));
	s = z * x;
	r = y + z*(s*(r + v) + y) + s*T[0];
	w = x + r;
	if (big) {
		s = 1 - 2*odd;
		v = s - 2.0 * (x + (r - w*w/(w + s)));
		return sign ? -v : v;
	}
	if (!odd)
		return w;

	w0 = w;
	SET_LOW_WORD(w0, 0);
	v = r - (w0 - x);
	a0 = a = -1.0 / w;
	SET_LOW_WORD(a0, 0);
	return a0 + a*(1.0 + a0*w0 + a0*v);
}
