#include "complex_impl.h"

double complex ccos(double complex z)
{
	return ccosh(CMPLX(-cimag(z), creal(z)));
}
