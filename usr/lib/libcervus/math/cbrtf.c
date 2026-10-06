#include <math.h>
#include <stdint.h>

static const unsigned
B1 = 709958130,
B2 = 642849266;

float cbrtf(float x)
{
	double_t r,T;
	union {float f; uint32_t i;} u = {x};
	uint32_t hx = u.i & 0x7fffffff;

	if (hx >= 0x7f800000)
		return x + x;

	if (hx < 0x00800000) {
		if (hx == 0)
			return x;
		u.f = x*0x1p24f;
		hx = u.i & 0x7fffffff;
		hx = hx/3 + B2;
	} else
		hx = hx/3 + B1;
	u.i &= 0x80000000;
	u.i |= hx;

	T = u.f;
	r = T*T*T;
	T = T*((double_t)x+x+r)/(x+r+r);

	r = T*T*T;
	T = T*((double_t)x+x+r)/(x+r+r);

	return T;
}
