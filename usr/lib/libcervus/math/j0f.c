#define _GNU_SOURCE
#include "libm.h"

static float pzerof(float), qzerof(float);

static const float
invsqrtpi = 5.6418961287e-01,
tpi       = 6.3661974669e-01;

static float common(uint32_t ix, float x, int y0)
{
	float z,s,c,ss,cc;

	s = sinf(x);
	c = cosf(x);
	if (y0)
		c = -c;
	cc = s+c;
	if (ix < 0x7f000000) {
		ss = s-c;
		z = -cosf(2*x);
		if (s*c < 0)
			cc = z/ss;
		else
			ss = z/cc;
		if (ix < 0x58800000) {
			if (y0)
				ss = -ss;
			cc = pzerof(x)*cc-qzerof(x)*ss;
		}
	}
	return invsqrtpi*cc/sqrtf(x);
}

static const float
R02 =  1.5625000000e-02,
R03 = -1.8997929874e-04,
R04 =  1.8295404516e-06,
R05 = -4.6183270541e-09,
S01 =  1.5619102865e-02,
S02 =  1.1692678527e-04,
S03 =  5.1354652442e-07,
S04 =  1.1661400734e-09;

float j0f(float x)
{
	float z,r,s;
	uint32_t ix;

	GET_FLOAT_WORD(ix, x);
	ix &= 0x7fffffff;
	if (ix >= 0x7f800000)
		return 1/(x*x);
	x = fabsf(x);

	if (ix >= 0x40000000) {
		return common(ix, x, 0);
	}
	if (ix >= 0x3a000000) {
		z = x*x;
		r = z*(R02+z*(R03+z*(R04+z*R05)));
		s = 1+z*(S01+z*(S02+z*(S03+z*S04)));
		return (1+x/2)*(1-x/2) + z*(r/s);
	}
	if (ix >= 0x21800000)
		x = 0.25f*x*x;
	return 1 - x;
}

static const float
u00  = -7.3804296553e-02,
u01  =  1.7666645348e-01,
u02  = -1.3818567619e-02,
u03  =  3.4745343146e-04,
u04  = -3.8140706238e-06,
u05  =  1.9559013964e-08,
u06  = -3.9820518410e-11,
v01  =  1.2730483897e-02,
v02  =  7.6006865129e-05,
v03  =  2.5915085189e-07,
v04  =  4.4111031494e-10;

float y0f(float x)
{
	float z,u,v;
	uint32_t ix;

	GET_FLOAT_WORD(ix, x);
	if ((ix & 0x7fffffff) == 0)
		return -1/0.0f;
	if (ix>>31)
		return 0/0.0f;
	if (ix >= 0x7f800000)
		return 1/x;
	if (ix >= 0x40000000) {
		return common(ix,x,1);
	}
	if (ix >= 0x39000000) {
		z = x*x;
		u = u00+z*(u01+z*(u02+z*(u03+z*(u04+z*(u05+z*u06)))));
		v = 1+z*(v01+z*(v02+z*(v03+z*v04)));
		return u/v + tpi*(j0f(x)*logf(x));
	}
	return u00 + tpi*logf(x);
}

static const float pR8[6] = {
  0.0000000000e+00,
 -7.0312500000e-02,
 -8.0816707611e+00,
 -2.5706311035e+02,
 -2.4852163086e+03,
 -5.2530439453e+03,
};
static const float pS8[5] = {
  1.1653436279e+02,
  3.8337448730e+03,
  4.0597855469e+04,
  1.1675296875e+05,
  4.7627726562e+04,
};
static const float pR5[6] = {
 -1.1412546255e-11,
 -7.0312492549e-02,
 -4.1596107483e+00,
 -6.7674766541e+01,
 -3.3123129272e+02,
 -3.4643338013e+02,
};
static const float pS5[5] = {
  6.0753936768e+01,
  1.0512523193e+03,
  5.9789707031e+03,
  9.6254453125e+03,
  2.4060581055e+03,
};

static const float pR3[6] = {
 -2.5470459075e-09,
 -7.0311963558e-02,
 -2.4090321064e+00,
 -2.1965976715e+01,
 -5.8079170227e+01,
 -3.1447946548e+01,
};
static const float pS3[5] = {
  3.5856033325e+01,
  3.6151397705e+02,
  1.1936077881e+03,
  1.1279968262e+03,
  1.7358093262e+02,
};

static const float pR2[6] = {
 -8.8753431271e-08,
 -7.0303097367e-02,
 -1.4507384300e+00,
 -7.6356959343e+00,
 -1.1193166733e+01,
 -3.2336456776e+00,
};
static const float pS2[5] = {
  2.2220300674e+01,
  1.3620678711e+02,
  2.7047027588e+02,
  1.5387539673e+02,
  1.4657617569e+01,
};

static float pzerof(float x)
{
	const float *p,*q;
	float_t z,r,s;
	uint32_t ix;

	GET_FLOAT_WORD(ix, x);
	ix &= 0x7fffffff;
	if      (ix >= 0x41000000){p = pR8; q = pS8;}
	else if (ix >= 0x409173eb){p = pR5; q = pS5;}
	else if (ix >= 0x4036d917){p = pR3; q = pS3;}
	else   {p = pR2; q = pS2;}
	z = 1.0f/(x*x);
	r = p[0]+z*(p[1]+z*(p[2]+z*(p[3]+z*(p[4]+z*p[5]))));
	s = 1.0f+z*(q[0]+z*(q[1]+z*(q[2]+z*(q[3]+z*q[4]))));
	return 1.0f + r/s;
}

static const float qR8[6] = {
  0.0000000000e+00,
  7.3242187500e-02,
  1.1768206596e+01,
  5.5767340088e+02,
  8.8591972656e+03,
  3.7014625000e+04,
};
static const float qS8[6] = {
  1.6377603149e+02,
  8.0983447266e+03,
  1.4253829688e+05,
  8.0330925000e+05,
  8.4050156250e+05,
 -3.4389928125e+05,
};

static const float qR5[6] = {
  1.8408595828e-11,
  7.3242180049e-02,
  5.8356351852e+00,
  1.3511157227e+02,
  1.0272437744e+03,
  1.9899779053e+03,
};
static const float qS5[6] = {
  8.2776611328e+01,
  2.0778142090e+03,
  1.8847289062e+04,
  5.6751113281e+04,
  3.5976753906e+04,
 -5.3543427734e+03,
};

static const float qR3[6] = {
  4.3774099900e-09,
  7.3241114616e-02,
  3.3442313671e+00,
  4.2621845245e+01,
  1.7080809021e+02,
  1.6673394775e+02,
};
static const float qS3[6] = {
  4.8758872986e+01,
  7.0968920898e+02,
  3.7041481934e+03,
  6.4604252930e+03,
  2.5163337402e+03,
 -1.4924745178e+02,
};

static const float qR2[6] = {
  1.5044444979e-07,
  7.3223426938e-02,
  1.9981917143e+00,
  1.4495602608e+01,
  3.1666231155e+01,
  1.6252708435e+01,
};
static const float qS2[6] = {
  3.0365585327e+01,
  2.6934811401e+02,
  8.4478375244e+02,
  8.8293585205e+02,
  2.1266638184e+02,
 -5.3109550476e+00,
};

static float qzerof(float x)
{
	const float *p,*q;
	float_t s,r,z;
	uint32_t ix;

	GET_FLOAT_WORD(ix, x);
	ix &= 0x7fffffff;
	if      (ix >= 0x41000000){p = qR8; q = qS8;}
	else if (ix >= 0x409173eb){p = qR5; q = qS5;}
	else if (ix >= 0x4036d917){p = qR3; q = qS3;}
	else   {p = qR2; q = qS2;}
	z = 1.0f/(x*x);
	r = p[0]+z*(p[1]+z*(p[2]+z*(p[3]+z*(p[4]+z*p[5]))));
	s = 1.0f+z*(q[0]+z*(q[1]+z*(q[2]+z*(q[3]+z*(q[4]+z*q[5])))));
	return (-.125f + r/s)/x;
}
