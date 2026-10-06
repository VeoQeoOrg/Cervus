#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double logl(long double x)
{
	return log(x);
}
#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384

static const long double P[] = {
 4.5270000862445199635215E-5L,
 4.9854102823193375972212E-1L,
 6.5787325942061044846969E0L,
 2.9911919328553073277375E1L,
 6.0949667980987787057556E1L,
 5.7112963590585538103336E1L,
 2.0039553499201281259648E1L,
};
static const long double Q[] = {
 1.5062909083469192043167E1L,
 8.3047565967967209469434E1L,
 2.2176239823732856465394E2L,
 3.0909872225312059774938E2L,
 2.1642788614495947685003E2L,
 6.0118660497603843919306E1L,
};

static const long double R[4] = {
 1.9757429581415468984296E-3L,
-7.1990767473014147232598E-1L,
 1.0777257190312272158094E1L,
-3.5717684488096787370998E1L,
};
static const long double S[4] = {
-2.6201045551331104417768E1L,
 1.9361891836232102174846E2L,
-4.2861221385716144629696E2L,
};
static const long double C1 = 6.9314575195312500000000E-1L;
static const long double C2 = 1.4286068203094172321215E-6L;

#define SQRTH 0.70710678118654752440L

long double logl(long double x)
{
	long double y, z;
	int e;

	if (isnan(x))
		return x;
	if (x == INFINITY)
		return x;
	if (x <= 0.0) {
		if (x == 0.0)
			return -1/(x*x);
		return 0/0.0f;
	}

	x = frexpl(x, &e);

	if (e > 2 || e < -2) {
		if (x < SQRTH) {
			e -= 1;
			z = x - 0.5;
			y = 0.5 * z + 0.5;
		} else {
			z = x - 0.5;
			z -= 0.5;
			y = 0.5 * x  + 0.5;
		}
		x = z / y;
		z = x*x;
		z = x * (z * __polevll(z, R, 3) / __p1evll(z, S, 3));
		z = z + e * C2;
		z = z + x;
		z = z + e * C1;
		return z;
	}

	if (x < SQRTH) {
		e -= 1;
		x = 2.0*x - 1.0;
	} else {
		x = x - 1.0;
	}
	z = x*x;
	y = x * (z * __polevll(x, P, 6) / __p1evll(x, Q, 6));
	y = y + e * C2;
	z = y - 0.5*z;

	z = z + x;
	z = z + e * C1;
	return z;
}
#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384

long double logl(long double x)
{
	return log(x);
}
#endif
