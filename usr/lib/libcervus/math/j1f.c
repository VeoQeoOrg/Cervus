#define _GNU_SOURCE
#include "libm.h"

static float ponef(float), qonef(float);

static const float
invsqrtpi = 5.6418961287e-01,
tpi       = 6.3661974669e-01;

static float common(uint32_t ix, float x, int y1, int sign)
{
	double z,s,c,ss,cc;

	s = sinf(x);
	if (y1)
		s = -s;
	c = cosf(x);
	cc = s-c;
	if (ix < 0x7f000000) {
		ss = -s-c;
		z = cosf(2*x);
		if (s*c > 0)
			cc = z/ss;
		else
			ss = z/cc;
		if (ix < 0x58800000) {
			if (y1)
				ss = -ss;
			cc = ponef(x)*cc-qonef(x)*ss;
		}
	}
	if (sign)
		cc = -cc;
	return invsqrtpi*cc/sqrtf(x);
}

static const float
r00 = -6.2500000000e-02,
r01 =  1.4070566976e-03,
r02 = -1.5995563444e-05,
r03 =  4.9672799207e-08,
s01 =  1.9153760746e-02,
s02 =  1.8594678841e-04,
s03 =  1.1771846857e-06,
s04 =  5.0463624390e-09,
s05 =  1.2354227016e-11;

float j1f(float x)
{
	float z,r,s;
	uint32_t ix;
	int sign;

	GET_FLOAT_WORD(ix, x);
	sign = ix>>31;
	ix &= 0x7fffffff;
	if (ix >= 0x7f800000)
		return 1/(x*x);
	if (ix >= 0x40000000)
		return common(ix, fabsf(x), 0, sign);
	if (ix >= 0x39000000) {
		z = x*x;
		r = z*(r00+z*(r01+z*(r02+z*r03)));
		s = 1+z*(s01+z*(s02+z*(s03+z*(s04+z*s05))));
		z = 0.5f + r/s;
	} else
		z = 0.5f;
	return z*x;
}

static const float U0[5] = {
 -1.9605709612e-01,
  5.0443872809e-02,
 -1.9125689287e-03,
  2.3525259166e-05,
 -9.1909917899e-08,
};
static const float V0[5] = {
  1.9916731864e-02,
  2.0255257550e-04,
  1.3560879779e-06,
  6.2274145840e-09,
  1.6655924903e-11,
};

float y1f(float x)
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
	if (ix >= 0x40000000)
		return common(ix,x,1,0);
	if (ix < 0x33000000)
		return -tpi/x;
	z = x*x;
	u = U0[0]+z*(U0[1]+z*(U0[2]+z*(U0[3]+z*U0[4])));
	v = 1.0f+z*(V0[0]+z*(V0[1]+z*(V0[2]+z*(V0[3]+z*V0[4]))));
	return x*(u/v) + tpi*(j1f(x)*logf(x)-1.0f/x);
}

static const float pr8[6] = {
  0.0000000000e+00,
  1.1718750000e-01,
  1.3239480972e+01,
  4.1205184937e+02,
  3.8747453613e+03,
  7.9144794922e+03,
};
static const float ps8[5] = {
  1.1420736694e+02,
  3.6509309082e+03,
  3.6956207031e+04,
  9.7602796875e+04,
  3.0804271484e+04,
};

static const float pr5[6] = {
  1.3199052094e-11,
  1.1718749255e-01,
  6.8027510643e+00,
  1.0830818176e+02,
  5.1763616943e+02,
  5.2871520996e+02,
};
static const float ps5[5] = {
  5.9280597687e+01,
  9.9140142822e+02,
  5.3532670898e+03,
  7.8446904297e+03,
  1.5040468750e+03,
};

static const float pr3[6] = {
  3.0250391081e-09,
  1.1718686670e-01,
  3.9329774380e+00,
  3.5119403839e+01,
  9.1055007935e+01,
  4.8559066772e+01,
};
static const float ps3[5] = {
  3.4791309357e+01,
  3.3676245117e+02,
  1.0468714600e+03,
  8.9081134033e+02,
  1.0378793335e+02,
};

static const float pr2[6] = {
  1.0771083225e-07,
  1.1717621982e-01,
  2.3685150146e+00,
  1.2242610931e+01,
  1.7693971634e+01,
  5.0735230446e+00,
};
static const float ps2[5] = {
  2.1436485291e+01,
  1.2529022980e+02,
  2.3227647400e+02,
  1.1767937469e+02,
  8.3646392822e+00,
};

static float ponef(float x)
{
	const float *p,*q;
	float_t z,r,s;
	uint32_t ix;

	GET_FLOAT_WORD(ix, x);
	ix &= 0x7fffffff;
	if      (ix >= 0x41000000){p = pr8; q = ps8;}
	else if (ix >= 0x409173eb){p = pr5; q = ps5;}
	else if (ix >= 0x4036d917){p = pr3; q = ps3;}
	else   {p = pr2; q = ps2;}
	z = 1.0f/(x*x);
	r = p[0]+z*(p[1]+z*(p[2]+z*(p[3]+z*(p[4]+z*p[5]))));
	s = 1.0f+z*(q[0]+z*(q[1]+z*(q[2]+z*(q[3]+z*q[4]))));
	return 1.0f + r/s;
}

static const float qr8[6] = {
  0.0000000000e+00,
 -1.0253906250e-01,
 -1.6271753311e+01,
 -7.5960174561e+02,
 -1.1849806641e+04,
 -4.8438511719e+04,
};
static const float qs8[6] = {
  1.6139537048e+02,
  7.8253862305e+03,
  1.3387534375e+05,
  7.1965775000e+05,
  6.6660125000e+05,
 -2.9449025000e+05,
};

static const float qr5[6] = {
 -2.0897993405e-11,
 -1.0253904760e-01,
 -8.0564479828e+00,
 -1.8366960144e+02,
 -1.3731937256e+03,
 -2.6124443359e+03,
};
static const float qs5[6] = {
  8.1276550293e+01,
  1.9917987061e+03,
  1.7468484375e+04,
  4.9851425781e+04,
  2.7948074219e+04,
 -4.7191835938e+03,
};

static const float qr3[6] = {
 -5.0783124372e-09,
 -1.0253783315e-01,
 -4.6101160049e+00,
 -5.7847221375e+01,
 -2.2824453735e+02,
 -2.1921012878e+02,
};
static const float qs3[6] = {
  4.7665153503e+01,
  6.7386511230e+02,
  3.3801528320e+03,
  5.5477290039e+03,
  1.9031191406e+03,
 -1.3520118713e+02,
};

static const float qr2[6] = {
 -1.7838172539e-07,
 -1.0251704603e-01,
 -2.7522056103e+00,
 -1.9663616180e+01,
 -4.2325313568e+01,
 -2.1371921539e+01,
};
static const float qs2[6] = {
  2.9533363342e+01,
  2.5298155212e+02,
  7.5750280762e+02,
  7.3939318848e+02,
  1.5594900513e+02,
 -4.9594988823e+00,
};

static float qonef(float x)
{
	const float *p,*q;
	float_t s,r,z;
	uint32_t ix;

	GET_FLOAT_WORD(ix, x);
	ix &= 0x7fffffff;
	if      (ix >= 0x41000000){p = qr8; q = qs8;}
	else if (ix >= 0x409173eb){p = qr5; q = qs5;}
	else if (ix >= 0x4036d917){p = qr3; q = qs3;}
	else   {p = qr2; q = qs2;}
	z = 1.0f/(x*x);
	r = p[0]+z*(p[1]+z*(p[2]+z*(p[3]+z*(p[4]+z*p[5]))));
	s = 1.0f+z*(q[0]+z*(q[1]+z*(q[2]+z*(q[3]+z*(q[4]+z*q[5])))));
	return (.375f + r/s)/x;
}
