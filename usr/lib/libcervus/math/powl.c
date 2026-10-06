#include "libm.h"

#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double powl(long double x, long double y)
{
	return pow(x, y);
}
#elif LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384

#define NXT 32

static const long double P[] = {
 8.3319510773868690346226E-4L,
 4.9000050881978028599627E-1L,
 1.7500123722550302671919E0L,
 1.4000100839971580279335E0L,
};
static const long double Q[] = {
 5.2500282295834889175431E0L,
 8.4000598057587009834666E0L,
 4.2000302519914740834728E0L,
};

static const long double A[33] = {
 1.0000000000000000000000E0L,
 9.7857206208770013448287E-1L,
 9.5760328069857364691013E-1L,
 9.3708381705514995065011E-1L,
 9.1700404320467123175367E-1L,
 8.9735453750155359320742E-1L,
 8.7812608018664974155474E-1L,
 8.5930964906123895780165E-1L,
 8.4089641525371454301892E-1L,
 8.2287773907698242225554E-1L,
 8.0524516597462715409607E-1L,
 7.8799042255394324325455E-1L,
 7.7110541270397041179298E-1L,
 7.5458221379671136985669E-1L,
 7.3841307296974965571198E-1L,
 7.2259040348852331001267E-1L,
 7.0710678118654752438189E-1L,
 6.9195494098191597746178E-1L,
 6.7712777346844636413344E-1L,
 6.6261832157987064729696E-1L,
 6.4841977732550483296079E-1L,
 6.3452547859586661129850E-1L,
 6.2092890603674202431705E-1L,
 6.0762367999023443907803E-1L,
 5.9460355750136053334378E-1L,
 5.8186242938878875689693E-1L,
 5.6939431737834582684856E-1L,
 5.5719337129794626814472E-1L,
 5.4525386633262882960438E-1L,
 5.3357020033841180906486E-1L,
 5.2213689121370692017331E-1L,
 5.1094857432705833910408E-1L,
 5.0000000000000000000000E-1L,
};
static const long double B[17] = {
 0.0000000000000000000000E0L,
 2.6176170809902549338711E-20L,
-1.0126791927256478897086E-20L,
 1.3438228172316276937655E-21L,
 1.2207982955417546912101E-20L,
-6.3084814358060867200133E-21L,
 1.3164426894366316434230E-20L,
-1.8527916071632873716786E-20L,
 1.8950325588932570796551E-20L,
 1.5564775779538780478155E-20L,
 6.0859793637556860974380E-21L,
-2.0208749253662532228949E-20L,
 1.4966292219224761844552E-20L,
 3.3540909728056476875639E-21L,
-8.6987564101742849540743E-22L,
-1.2327176863327626135542E-20L,
 0.0000000000000000000000E0L,
};

static const long double R[] = {
 1.5089970579127659901157E-5L,
 1.5402715328927013076125E-4L,
 1.3333556028915671091390E-3L,
 9.6181291046036762031786E-3L,
 5.5504108664798463044015E-2L,
 2.4022650695910062854352E-1L,
 6.9314718055994530931447E-1L,
};

#define MEXP (NXT*16384.0L)

#define MNEXP (-NXT*(16384.0L+64.0L))

#define LOG2EA 0.44269504088896340735992L

#define F W
#define Fa Wa
#define Fb Wb
#define G W
#define Ga Wa
#define Gb u
#define H W
#define Ha Wb
#define Hb Wb

static const long double MAXLOGL = 1.1356523406294143949492E4L;
static const long double MINLOGL = -1.13994985314888605586758E4L;
static const long double LOGE2L = 6.9314718055994530941723E-1L;
static const long double huge = 0x1p10000L;

static const volatile long double twom10000 = 0x1p-10000L;

static long double reducl(long double);
static long double powil(long double, int);

long double powl(long double x, long double y)
{
	int i, nflg, iyflg, yoddint;
	long e;
	volatile long double z=0;
	long double w=0, W=0, Wa=0, Wb=0, ya=0, yb=0, u=0;

	if (isnan(x)) {
		if (!isnan(y) && y == 0.0)
			return 1.0;
		return x;
	}
	if (isnan(y)) {
		if (x == 1.0)
			return 1.0;
		return y;
	}
	if (x == 1.0)
		return 1.0;
	if (y == 0.0)
		return 1.0;
	if (y == 1.0)
		return x;

	if (fabsl(y) > 2*(-LDBL_MIN_EXP+LDBL_MANT_DIG+1)/LDBL_EPSILON) {
		if (x == -1.0)
			return 1.0;
		if (y == INFINITY) {
			if (x > 1.0 || x < -1.0)
				return INFINITY;
			return 0.0;
		}
		if (y == -INFINITY) {
			if (x > 1.0 || x < -1.0)
				return 0.0;
			return INFINITY;
		}
		if ((x > 1.0 || x < -1.0) == (y > 0))
			return huge * huge;
		return twom10000 * twom10000;
	}
	if (x == INFINITY) {
		if (y > 0.0)
			return INFINITY;
		return 0.0;
	}

	w = floorl(y);

	iyflg = 0;
	if (w == y)
		iyflg = 1;

	yoddint = 0;
	if (iyflg) {
		ya = fabsl(y);
		ya = floorl(0.5 * ya);
		yb = 0.5 * fabsl(w);
		if( ya != yb )
			yoddint = 1;
	}

	if (x == -INFINITY) {
		if (y > 0.0) {
			if (yoddint)
				return -INFINITY;
			return INFINITY;
		}
		if (y < 0.0) {
			if (yoddint)
				return -0.0;
			return 0.0;
		}
	}
	nflg = 0;
	if (x <= 0.0) {
		if (x == 0.0) {
			if (y < 0.0) {
				if (signbit(x) && yoddint)

					return -1.0/0.0;

				return 1.0/0.0;
			}
			if (signbit(x) && yoddint)
				return -0.0;
			return 0.0;
		}
		if (iyflg == 0)
			return (x - x) / (x - x);

		if (yoddint)
			nflg = 1;
		x = -x;
	}

	if (iyflg && floorl(x) == x && fabsl(y) < 32768.0) {
		w = powil(x, (int)y);
		return nflg ? -w : w;
	}

	x = frexpl(x, &i);
	e = i;

	i = 1;
	if (x <= A[17])
		i = 17;
	if (x <= A[i+8])
		i += 8;
	if (x <= A[i+4])
		i += 4;
	if (x <= A[i+2])
		i += 2;
	if (x >= A[1])
		i = -1;
	i += 1;

	x -= A[i];
	x -= B[i/2];
	x /= A[i];

	z = x*x;
	w = x * (z * __polevll(x, P, 3) / __p1evll(x, Q, 3));
	w = w - 0.5*z;

	z = LOG2EA * w;
	z += w;
	z += LOG2EA * x;
	z += x;

	w = -i;
	w /= NXT;
	w += e;

	ya = reducl(y);
	yb = y - ya;

	F = z * y  +  w * yb;
	Fa = reducl(F);
	Fb = F - Fa;

	G = Fa + w * ya;
	Ga = reducl(G);
	Gb = G - Ga;

	H = Fb + Gb;
	Ha = reducl(H);
	w = (Ga + Ha) * NXT;

	if (w > MEXP)
		return huge * huge;
	if (w < MNEXP)
		return twom10000 * twom10000;

	e = w;
	Hb = H - Ha;

	if (Hb > 0.0) {
		e += 1;
		Hb -= 1.0/NXT;
	}

	z = Hb * __polevll(Hb, R, 6);

	if (e < 0)
		i = 0;
	else
		i = 1;
	i = e/NXT + i;
	e = NXT*i - e;
	w = A[e];
	z = w * z;
	z = z + w;
	z = scalbnl(z, i);

	if (nflg)
		z = -z;
	return z;
}

static long double reducl(long double x)
{
	long double t;

	t = x * NXT;
	t = floorl(t);
	t = t / NXT;
	return t;
}

static long double powil(long double x, int nn)
{
	long double ww, y;
	long double s;
	int n, e, sign, lx;

	if (nn == 0)
		return 1.0;

	if (nn < 0) {
		sign = -1;
		n = -nn;
	} else {
		sign = 1;
		n = nn;
	}

	s = x;
	s = frexpl( s, &lx);
	e = (lx - 1)*n;
	if ((e == 0) || (e > 64) || (e < -64)) {
		s = (s - 7.0710678118654752e-1L) / (s +  7.0710678118654752e-1L);
		s = (2.9142135623730950L * s - 0.5 + lx) * nn * LOGE2L;
	} else {
		s = LOGE2L * e;
	}

	if (s > MAXLOGL)
		return huge * huge;

	if (s < MINLOGL)
		return twom10000 * twom10000;

	if (s < -MAXLOGL+2.0) {
		x = 1.0/x;
		sign = -sign;
	}

	if (n & 1)
		y = x;
	else
		y = 1.0;

	ww = x;
	n >>= 1;
	while (n) {
		ww = ww * ww;
		if (n & 1)
			y *= ww;
		n >>= 1;
	}

	if (sign < 0)
		y = 1.0/y;
	return y;
}
#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384

long double powl(long double x, long double y)
{
	return pow(x, y);
}
#endif
