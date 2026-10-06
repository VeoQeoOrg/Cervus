#include <math.h>

float fmaxf(float x, float y)
{
	if (isnan(x))
		return y;
	if (isnan(y))
		return x;

	if (signbit(x) != signbit(y))
		return signbit(x) ? y : x;
	return x < y ? y : x;
}
