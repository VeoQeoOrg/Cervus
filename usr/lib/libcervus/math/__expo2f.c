#include "libm.h"

static const int k = 235;
static const float kln2 = 0x1.45c778p+7f;

float __expo2f(float x, float sign)
{
	float scale;

	SET_FLOAT_WORD(scale, (uint32_t)(0x7f + k/2) << 23);

	return expf(x - kln2) * (sign * scale) * scale;
}
