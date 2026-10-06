#include "complex_impl.h"

double complex ctan(double complex z)
{
	z = ctanh(CMPLX(-cimag(z), creal(z)));
	return CMPLX(cimag(z), -creal(z));
}
