#include "libm.h"
#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
long double fmal(long double x, long double y, long double z)
{
	return fma(x, y, z);
}
#elif (LDBL_MANT_DIG == 64 || LDBL_MANT_DIG == 113) && LDBL_MAX_EXP == 16384
#include <fenv.h>
#if LDBL_MANT_DIG == 64
#define LASTBIT(u) (u.i.m & 1)
#define SPLIT (0x1p32L + 1)
#elif LDBL_MANT_DIG == 113
#define LASTBIT(u) (u.i.lo & 1)
#define SPLIT (0x1p57L + 1)
#endif

struct dd {
	long double hi;
	long double lo;
};

static inline struct dd dd_add(long double a, long double b)
{
	struct dd ret;
	long double s;

	ret.hi = a + b;
	s = ret.hi - a;
	ret.lo = (a - (ret.hi - s)) + (b - s);
	return (ret);
}

static inline long double add_adjusted(long double a, long double b)
{
	struct dd sum;
	union ldshape u;

	sum = dd_add(a, b);
	if (sum.lo != 0) {
		u.f = sum.hi;
		if (!LASTBIT(u))
			sum.hi = nextafterl(sum.hi, INFINITY * sum.lo);
	}
	return (sum.hi);
}

static inline long double add_and_denormalize(long double a, long double b, int scale)
{
	struct dd sum;
	int bits_lost;
	union ldshape u;

	sum = dd_add(a, b);

	if (sum.lo != 0) {
		u.f = sum.hi;
		bits_lost = -u.i.se - scale + 1;
		if ((bits_lost != 1) ^ LASTBIT(u))
			sum.hi = nextafterl(sum.hi, INFINITY * sum.lo);
	}
	return scalbnl(sum.hi, scale);
}

static inline struct dd dd_mul(long double a, long double b)
{
	struct dd ret;
	long double ha, hb, la, lb, p, q;

	p = a * SPLIT;
	ha = a - p;
	ha += p;
	la = a - ha;

	p = b * SPLIT;
	hb = b - p;
	hb += p;
	lb = b - hb;

	p = ha * hb;
	q = ha * lb + la * hb;

	ret.hi = p + q;
	ret.lo = p - ret.hi + q + la * lb;
	return (ret);
}

long double fmal(long double x, long double y, long double z)
{
	#pragma STDC FENV_ACCESS ON
	long double xs, ys, zs, adj;
	struct dd xy, r;
	int oround;
	int ex, ey, ez;
	int spread;

	if (!isfinite(x) || !isfinite(y))
		return (x * y + z);
	if (!isfinite(z))
		return (z);
	if (x == 0.0 || y == 0.0)
		return (x * y + z);
	if (z == 0.0)
		return (x * y);

	xs = frexpl(x, &ex);
	ys = frexpl(y, &ey);
	zs = frexpl(z, &ez);
	oround = fegetround();
	spread = ex + ey - ez;

	if (spread < -LDBL_MANT_DIG) {
#ifdef FE_INEXACT
		feraiseexcept(FE_INEXACT);
#endif
#ifdef FE_UNDERFLOW
		if (!isnormal(z))
			feraiseexcept(FE_UNDERFLOW);
#endif
		switch (oround) {
		default:
			return (z);
#ifdef FE_TOWARDZERO
		case FE_TOWARDZERO:
			if (x > 0.0 ^ y < 0.0 ^ z < 0.0)
				return (z);
			else
				return (nextafterl(z, 0));
#endif
#ifdef FE_DOWNWARD
		case FE_DOWNWARD:
			if (x > 0.0 ^ y < 0.0)
				return (z);
			else
				return (nextafterl(z, -INFINITY));
#endif
#ifdef FE_UPWARD
		case FE_UPWARD:
			if (x > 0.0 ^ y < 0.0)
				return (nextafterl(z, INFINITY));
			else
				return (z);
#endif
		}
	}
	if (spread <= LDBL_MANT_DIG * 2)
		zs = scalbnl(zs, -spread);
	else
		zs = copysignl(LDBL_MIN, zs);

	fesetround(FE_TONEAREST);

	xy = dd_mul(xs, ys);
	r = dd_add(xy.hi, zs);

	spread = ex + ey;

	if (r.hi == 0.0) {
		fesetround(oround);
		volatile long double vzs = zs;
		return xy.hi + vzs + scalbnl(xy.lo, spread);
	}

	if (oround != FE_TONEAREST) {
		long double ret;
#if defined(FE_INEXACT) && defined(FE_UNDERFLOW)
		int e = fetestexcept(FE_INEXACT);
		feclearexcept(FE_INEXACT);
#endif
		fesetround(oround);
		adj = r.lo + xy.lo;
		ret = scalbnl(r.hi + adj, spread);
#if defined(FE_INEXACT) && defined(FE_UNDERFLOW)
		if (ilogbl(ret) < -16382 && fetestexcept(FE_INEXACT))
			feraiseexcept(FE_UNDERFLOW);
		else if (e)
			feraiseexcept(FE_INEXACT);
#endif
		return ret;
	}

	adj = add_adjusted(r.lo, xy.lo);
	if (spread + ilogbl(r.hi) > -16383)
		return scalbnl(r.hi + adj, spread);
	else
		return add_and_denormalize(r.hi, adj, spread);
}
#endif
