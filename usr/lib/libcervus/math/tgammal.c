#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double tgammal(long double x)
{
	return tgamma(x);
}
#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384

static const long double P[8] = {
 4.212760487471622013093E-5L,
 4.542931960608009155600E-4L,
 4.092666828394035500949E-3L,
 2.385363243461108252554E-2L,
 1.113062816019361559013E-1L,
 3.629515436640239168939E-1L,
 8.378004301573126728826E-1L,
 1.000000000000000000009E0L,
};
static const long double Q[9] = {
-1.397148517476170440917E-5L,
 2.346584059160635244282E-4L,
-1.237799246653152231188E-3L,
-7.955933682494738320586E-4L,
 2.773706565840072979165E-2L,
-4.633887671244534213831E-2L,
-2.243510905670329164562E-1L,
 4.150160950588455434583E-1L,
 9.999999999999999999908E-1L,
};

#define MAXGAML 1755.455L

static const long double STIR[9] = {
 7.147391378143610789273E-4L,
-2.363848809501759061727E-5L,
-5.950237554056330156018E-4L,
 6.989332260623193171870E-5L,
 7.840334842744753003862E-4L,
-2.294719747873185405699E-4L,
-2.681327161876304418288E-3L,
 3.472222222230075327854E-3L,
 8.333333333333331800504E-2L,
};

#define MAXSTIR 1024.0L
static const long double SQTPI = 2.50662827463100050242E0L;

static const long double S[9] = {
-1.193945051381510095614E-3L,
 7.220599478036909672331E-3L,
-9.622023360406271645744E-3L,
-4.219773360705915470089E-2L,
 1.665386113720805206758E-1L,
-4.200263503403344054473E-2L,
-6.558780715202540684668E-1L,
 5.772156649015328608253E-1L,
 1.000000000000000000000E0L,
};

static const long double SN[9] = {
 1.133374167243894382010E-3L,
 7.220837261893170325704E-3L,
 9.621911155035976733706E-3L,
-4.219773343731191721664E-2L,
-1.665386113944413519335E-1L,
-4.200263503402112910504E-2L,
 6.558780715202536547116E-1L,
 5.772156649015328608727E-1L,
-1.000000000000000000000E0L,
};

static const long double PIL = 3.1415926535897932384626L;

static long double stirf(long double x)
{
	long double y, w, v;

	w = 1.0/x;

	if (x > 1024.0)
		w = (((((6.97281375836585777429E-5L * w
		 + 7.84039221720066627474E-4L) * w
		 - 2.29472093621399176955E-4L) * w
		 - 2.68132716049382716049E-3L) * w
		 + 3.47222222222222222222E-3L) * w
		 + 8.33333333333333333333E-2L) * w
		 + 1.0;
	else
		w = 1.0 + w * __polevll(w, STIR, 8);
	y = expl(x);
	if (x > MAXSTIR) {
		v = powl(x, 0.5L * x - 0.25L);
		y = v * (v / y);
	} else {
		y = powl(x, x - 0.5L) / y;
	}
	y = SQTPI * y * w;
	return y;
}

long double tgammal(long double x)
{
	long double p, q, z;

	if (!isfinite(x))
		return x + INFINITY;

	q = fabsl(x);
	if (q > 13.0) {
		if (x < 0.0) {
			p = floorl(q);
			z = q - p;
			if (z == 0)
				return 0 / z;
			if (q > MAXGAML) {
				z = 0;
			} else {
				if (z > 0.5) {
					p += 1.0;
					z = q - p;
				}
				z = q * sinl(PIL * z);
				z = fabsl(z) * stirf(q);
				z = PIL/z;
			}
			if (0.5 * p == floorl(q * 0.5))
				z = -z;
		} else if (x > MAXGAML) {
			z = x * 0x1p16383L;
		} else {
			z = stirf(x);
		}
		return z;
	}

	z = 1.0;
	while (x >= 3.0) {
		x -= 1.0;
		z *= x;
	}
	while (x < -0.03125L) {
		z /= x;
		x += 1.0;
	}
	if (x <= 0.03125L)
		goto small;
	while (x < 2.0) {
		z /= x;
		x += 1.0;
	}
	if (x == 2.0)
		return z;

	x -= 2.0;
	p = __polevll(x, P, 7);
	q = __polevll(x, Q, 8);
	z = z * p / q;
	return z;

small:

	if (x == 0 && z != 1)
		return x / x;
	if (x < 0.0) {
		x = -x;
		q = z / (x * __polevll(x, SN, 8));
	} else
		q = z / (x * __polevll(x, S, 8));
	return q;
}
#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384

long double tgammal(long double x)
{
	return tgamma(x);
}
#endif
