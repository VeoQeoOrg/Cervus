#include "complex_impl.h"

double complex csin(double complex z)
{
	z = csinh(CMPLX(-cimag(z), creal(z)));
	return CMPLX(cimag(z), -creal(z));
}
