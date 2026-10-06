#include "libm.h"

static const float
erx  =  8.4506291151e-01,

efx8 =  1.0270333290e+00,
pp0  =  1.2837916613e-01,
pp1  = -3.2504209876e-01,
pp2  = -2.8481749818e-02,
pp3  = -5.7702702470e-03,
pp4  = -2.3763017452e-05,
qq1  =  3.9791721106e-01,
qq2  =  6.5022252500e-02,
qq3  =  5.0813062117e-03,
qq4  =  1.3249473704e-04,
qq5  = -3.9602282413e-06,

pa0  = -2.3621185683e-03,
pa1  =  4.1485610604e-01,
pa2  = -3.7220788002e-01,
pa3  =  3.1834661961e-01,
pa4  = -1.1089469492e-01,
pa5  =  3.5478305072e-02,
pa6  = -2.1663755178e-03,
qa1  =  1.0642088205e-01,
qa2  =  5.4039794207e-01,
qa3  =  7.1828655899e-02,
qa4  =  1.2617121637e-01,
qa5  =  1.3637083583e-02,
qa6  =  1.1984500103e-02,

ra0  = -9.8649440333e-03,
ra1  = -6.9385856390e-01,
ra2  = -1.0558626175e+01,
ra3  = -6.2375331879e+01,
ra4  = -1.6239666748e+02,
ra5  = -1.8460508728e+02,
ra6  = -8.1287437439e+01,
ra7  = -9.8143291473e+00,
sa1  =  1.9651271820e+01,
sa2  =  1.3765776062e+02,
sa3  =  4.3456588745e+02,
sa4  =  6.4538726807e+02,
sa5  =  4.2900814819e+02,
sa6  =  1.0863500214e+02,
sa7  =  6.5702495575e+00,
sa8  = -6.0424413532e-02,

rb0  = -9.8649431020e-03,
rb1  = -7.9928326607e-01,
rb2  = -1.7757955551e+01,
rb3  = -1.6063638306e+02,
rb4  = -6.3756646729e+02,
rb5  = -1.0250950928e+03,
rb6  = -4.8351919556e+02,
sb1  =  3.0338060379e+01,
sb2  =  3.2579251099e+02,
sb3  =  1.5367296143e+03,
sb4  =  3.1998581543e+03,
sb5  =  2.5530502930e+03,
sb6  =  4.7452853394e+02,
sb7  = -2.2440952301e+01;

static float erfc1(float x)
{
	float_t s,P,Q;

	s = fabsf(x) - 1;
	P = pa0+s*(pa1+s*(pa2+s*(pa3+s*(pa4+s*(pa5+s*pa6)))));
	Q = 1+s*(qa1+s*(qa2+s*(qa3+s*(qa4+s*(qa5+s*qa6)))));
	return 1 - erx - P/Q;
}

static float erfc2(uint32_t ix, float x)
{
	float_t s,R,S;
	float z;

	if (ix < 0x3fa00000)
		return erfc1(x);

	x = fabsf(x);
	s = 1/(x*x);
	if (ix < 0x4036db6d) {
		R = ra0+s*(ra1+s*(ra2+s*(ra3+s*(ra4+s*(
		     ra5+s*(ra6+s*ra7))))));
		S = 1.0f+s*(sa1+s*(sa2+s*(sa3+s*(sa4+s*(
		     sa5+s*(sa6+s*(sa7+s*sa8)))))));
	} else {
		R = rb0+s*(rb1+s*(rb2+s*(rb3+s*(rb4+s*(
		     rb5+s*rb6)))));
		S = 1.0f+s*(sb1+s*(sb2+s*(sb3+s*(sb4+s*(
		     sb5+s*(sb6+s*sb7))))));
	}
	GET_FLOAT_WORD(ix, x);
	SET_FLOAT_WORD(z, ix&0xffffe000);
	return expf(-z*z - 0.5625f) * expf((z-x)*(z+x) + R/S)/x;
}

float erff(float x)
{
	float r,s,z,y;
	uint32_t ix;
	int sign;

	GET_FLOAT_WORD(ix, x);
	sign = ix>>31;
	ix &= 0x7fffffff;
	if (ix >= 0x7f800000) {
		return 1-2*sign + 1/x;
	}
	if (ix < 0x3f580000) {
		if (ix < 0x31800000) {
			return 0.125f*(8*x + efx8*x);
		}
		z = x*x;
		r = pp0+z*(pp1+z*(pp2+z*(pp3+z*pp4)));
		s = 1+z*(qq1+z*(qq2+z*(qq3+z*(qq4+z*qq5))));
		y = r/s;
		return x + x*y;
	}
	if (ix < 0x40c00000)
		y = 1 - erfc2(ix,x);
	else
		y = 1 - 0x1p-120f;
	return sign ? -y : y;
}

float erfcf(float x)
{
	float r,s,z,y;
	uint32_t ix;
	int sign;

	GET_FLOAT_WORD(ix, x);
	sign = ix>>31;
	ix &= 0x7fffffff;
	if (ix >= 0x7f800000) {
		return 2*sign + 1/x;
	}

	if (ix < 0x3f580000) {
		if (ix < 0x23800000)
			return 1.0f - x;
		z = x*x;
		r = pp0+z*(pp1+z*(pp2+z*(pp3+z*pp4)));
		s = 1.0f+z*(qq1+z*(qq2+z*(qq3+z*(qq4+z*qq5))));
		y = r/s;
		if (sign || ix < 0x3e800000)
			return 1.0f - (x+x*y);
		return 0.5f - (x - 0.5f + x*y);
	}
	if (ix < 0x41e00000) {
		return sign ? 2 - erfc2(ix,x) : erfc2(ix,x);
	}
	return sign ? 2 - 0x1p-120f : 0x1p-120f*0x1p-120f;
}
