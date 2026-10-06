#include <stdint.h>
#include <float.h>
#include <math.h>
#include "atomic.h"

#define ASUINT64(x) ((union {double f; uint64_t i;}){x}).i
#define ZEROINFNAN (0x7ff-0x3ff-52-1)

struct num { uint64_t m; int e; int sign; };

static struct num normalize(double x)
{
	uint64_t ix = ASUINT64(x);
	int e = ix>>52;
	int sign = e & 0x800;
	e &= 0x7ff;
	if (!e) {
		ix = ASUINT64(x*0x1p63);
		e = ix>>52 & 0x7ff;
		e = e ? e-63 : 0x800;
	}
	ix &= (1ull<<52)-1;
	ix |= 1ull<<52;
	ix <<= 1;
	e -= 0x3ff + 52 + 1;
	return (struct num){ix,e,sign};
}

static void mul(uint64_t *hi, uint64_t *lo, uint64_t x, uint64_t y)
{
	uint64_t t1,t2,t3;
	uint64_t xlo = (uint32_t)x, xhi = x>>32;
	uint64_t ylo = (uint32_t)y, yhi = y>>32;

	t1 = xlo*ylo;
	t2 = xlo*yhi + xhi*ylo;
	t3 = xhi*yhi;
	*lo = t1 + (t2<<32);
	*hi = t3 + (t2>>32) + (t1 > *lo);
}

double fma(double x, double y, double z)
{
	#pragma STDC FENV_ACCESS ON

	struct num nx, ny, nz;
	nx = normalize(x);
	ny = normalize(y);
	nz = normalize(z);

	if (nx.e >= ZEROINFNAN || ny.e >= ZEROINFNAN)
		return x*y + z;
	if (nz.e >= ZEROINFNAN) {
		if (nz.e > ZEROINFNAN)
			return x*y;
		return z;
	}

	uint64_t rhi, rlo, zhi, zlo;
	mul(&rhi, &rlo, nx.m, ny.m);

	int e = nx.e + ny.e;
	int d = nz.e - e;

	if (d > 0) {
		if (d < 64) {
			zlo = nz.m<<d;
			zhi = nz.m>>64-d;
		} else {
			zlo = 0;
			zhi = nz.m;
			e = nz.e - 64;
			d -= 64;
			if (d == 0) {
			} else if (d < 64) {
				rlo = rhi<<64-d | rlo>>d | !!(rlo<<64-d);
				rhi = rhi>>d;
			} else {
				rlo = 1;
				rhi = 0;
			}
		}
	} else {
		zhi = 0;
		d = -d;
		if (d == 0) {
			zlo = nz.m;
		} else if (d < 64) {
			zlo = nz.m>>d | !!(nz.m<<64-d);
		} else {
			zlo = 1;
		}
	}

	int sign = nx.sign^ny.sign;
	int samesign = !(sign^nz.sign);
	int nonzero = 1;
	if (samesign) {
		rlo += zlo;
		rhi += zhi + (rlo < zlo);
	} else {
		uint64_t t = rlo;
		rlo -= zlo;
		rhi = rhi - zhi - (t < rlo);
		if (rhi>>63) {
			rlo = -rlo;
			rhi = -rhi-!!rlo;
			sign = !sign;
		}
		nonzero = !!rhi;
	}

	if (nonzero) {
		e += 64;
		d = a_clz_64(rhi)-1;

		rhi = rhi<<d | rlo>>64-d | !!(rlo<<d);
	} else if (rlo) {
		d = a_clz_64(rlo)-1;
		if (d < 0)
			rhi = rlo>>1 | (rlo&1);
		else
			rhi = rlo<<d;
	} else {
		return x*y + z;
	}
	e -= d;

	int64_t i = rhi;
	if (sign)
		i = -i;
	double r = i;

	if (e < -1022-62) {
		if (e == -1022-63) {
			double c = 0x1p63;
			if (sign)
				c = -c;
			if (r == c) {
				float fltmin = 0x0.ffffff8p-63*FLT_MIN * r;
				return DBL_MIN/FLT_MIN * fltmin;
			}

			if (rhi << 53) {
				i = rhi>>1 | (rhi&1) | 1ull<<62;
				if (sign)
					i = -i;
				r = i;
				r = 2*r - c;

				{
					double_t tiny = DBL_MIN/FLT_MIN * r;
					r += (double)(tiny*tiny) * (r-r);
				}
			}
		} else {
			d = 10;
			i = ( rhi>>d | !!(rhi<<64-d) ) << d;
			if (sign)
				i = -i;
			r = i;
		}
	}
	return scalbn(r, e);
}
