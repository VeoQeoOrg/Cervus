#include "complex_impl.h"

double complex casinh(double complex z)
{
	z = casin(CMPLX(-cimag(z), creal(z)));
	return CMPLX(cimag(z), -creal(z));
}
