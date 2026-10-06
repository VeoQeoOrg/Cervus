#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double log2l(long double x)
{
	return log2(x);
}
#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384

static const long double P[] = {
 4.9962495940332550844739E-1L,
 1.0767376367209449010438E1L,
 7.7671073698359539859595E1L,
 2.5620629828144409632571E2L,
 4.2401812743503691187826E2L,
 3.4258224542413922935104E2L,
 1.0747524399916215149070E2L,
};
static const long double Q[] = {
 2.3479774160285863271658E1L,
 1.9444210022760132894510E2L,
 7.7952888181207260646090E2L,
 1.6911722418503949084863E3L,
 2.0307734695595183428202E3L,
 1.2695660352705325274404E3L,
 3.2242573199748645407652E2L,
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

#define LOG2EA 4.4269504088896340735992e-1L

#define SQRTH 0.70710678118654752440L

long double log2l(long double x)
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
			y = 0.5 * x + 0.5;
		}
		x = z / y;
		z = x*x;
		y = x * (z * __polevll(z, R, 3) / __p1evll(z, S, 3));
		goto done;
	}

	if (x < SQRTH) {
		e -= 1;
		x = 2.0*x - 1.0;
	} else {
		x = x - 1.0;
	}
	z = x*x;
	y = x * (z * __polevll(x, P, 6) / __p1evll(x, Q, 7));
	y = y - 0.5*z;

done:

	z = y * LOG2EA;
	z += x * LOG2EA;
	z += y;
	z += x;
	z += e;
	return z;
}
#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384

long double log2l(long double x)
{
	return log2(x);
}
#endif
