#include "complex_impl.h"

double complex cacosh(double complex z)
{
	int zineg = signbit(cimag(z));

	z = cacos(z);
	if (zineg) return CMPLX(cimag(z), -creal(z));
	else       return CMPLX(-cimag(z), creal(z));
}
