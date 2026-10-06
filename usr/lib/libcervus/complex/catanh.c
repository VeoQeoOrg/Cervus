#include "complex_impl.h"

double complex catanh(double complex z)
{
	z = catan(CMPLX(-cimag(z), creal(z)));
	return CMPLX(cimag(z), -creal(z));
}
