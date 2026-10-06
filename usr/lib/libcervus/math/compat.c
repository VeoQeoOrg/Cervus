#include <math.h>

int __fpclassifyd(double x)
{
	return __fpclassify(x);
}

int __signbitd(double x)
{
	return __signbit(x);
}

#undef isnan
#undef isinf

int isnan(double x)
{
	return __fpclassify(x) == FP_NAN;
}

int isinf(double x)
{
	return __fpclassify(x) == FP_INFINITE;
}
