#include "libm.h"

static const double
S1  = -1.66666666666666324348e-01,
S2  =  8.33333333332248946124e-03,
S3  = -1.98412698298579493134e-04,
S4  =  2.75573137070700676789e-06,
S5  = -2.50507602534068634195e-08,
S6  =  1.58969099521155010221e-10;

double __sin(double x, double y, int iy)
{
	double_t z,r,v,w;

	z = x*x;
	w = z*z;
	r = S2 + z*(S3 + z*S4) + z*w*(S5 + z*S6);
	v = z*x;
	if (iy == 0)
		return x + v*(S1 + z*r);
	else
		return x - ((z*(0.5*y - v*r) - y) - v*S1);
}
