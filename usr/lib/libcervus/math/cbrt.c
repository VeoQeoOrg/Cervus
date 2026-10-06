#include <math.h>
#include <stdint.h>

static const uint32_t
B1 = 715094163,
B2 = 696219795;

static const double
P0 =  1.87595182427177009643,
P1 = -1.88497979543377169875,
P2 =  1.621429720105354466140,
P3 = -0.758397934778766047437,
P4 =  0.145996192886612446982;

double cbrt(double x)
{
	union {double f; uint64_t i;} u = {x};
	double_t r,s,t,w;
	uint32_t hx = u.i>>32 & 0x7fffffff;

	if (hx >= 0x7ff00000)
		return x+x;

	if (hx < 0x00100000) {
		u.f = x*0x1p54;
		hx = u.i>>32 & 0x7fffffff;
		if (hx == 0)
			return x;
		hx = hx/3 + B2;
	} else
		hx = hx/3 + B1;
	u.i &= 1ULL<<63;
	u.i |= (uint64_t)hx << 32;
	t = u.f;

	r = (t*t)*(t/x);
	t = t*((P0+r*(P1+r*P2))+((r*r)*r)*(P3+r*P4));

	u.f = t;
	u.i = (u.i + 0x80000000) & 0xffffffffc0000000ULL;
	t = u.f;

	s = t*t;
	r = x/s;
	w = t+t;
	r = (r-t)/(w+r);
	t = t+t*r;
	return t;
}
