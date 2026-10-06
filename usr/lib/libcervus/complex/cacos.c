#include "complex_impl.h"

double complex cacos(double complex z)
{
	z = casin(z);
	return CMPLX(M_PI_2 - creal(z), -cimag(z));
}
