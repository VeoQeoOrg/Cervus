#include <math.h>
#include "darwin.h"

struct dsincos {
    double s, c;
};

struct dsincosf {
    float s, c;
};

static struct dsincos d___sincos_stret(double x)
{
    struct dsincos r;
    sincos(x, &r.s, &r.c);
    return r;
}

static struct dsincosf d___sincosf_stret(float x)
{
    struct dsincosf r;
    sincosf(x, &r.s, &r.c);
    return r;
}

static struct dsincos d___sincospi_stret(double x)
{
    struct dsincos r;
    sincos(x * M_PI, &r.s, &r.c);
    return r;
}

static double d___sinpi(double x) { return sin(x * M_PI); }
static double d___cospi(double x) { return cos(x * M_PI); }
static double d___tanpi(double x) { return tan(x * M_PI); }

static int dclass(int c)
{
    switch (c) {
    case FP_NAN: return 1;
    case FP_INFINITE: return 2;
    case FP_ZERO: return 3;
    case FP_SUBNORMAL: return 5;
    default: return 4;
    }
}

static int d___fpclassifyd(double x) { return dclass(fpclassify(x)); }
static int d___fpclassifyf(float x) { return dclass(fpclassify(x)); }
static int d___fpclassifyl(long double x) { return dclass(fpclassify(x)); }

DSYM("___sincos_stret", d___sincos_stret);
DSYM("___sincosf_stret", d___sincosf_stret);
DSYM("___sincospi_stret", d___sincospi_stret);
DSYM("___sinpi", d___sinpi);
DSYM("___cospi", d___cospi);
DSYM("___tanpi", d___tanpi);
DSYM("___fpclassifyd", d___fpclassifyd);
DSYM("___fpclassifyf", d___fpclassifyf);
DSYM("___fpclassifyl", d___fpclassifyl);
DSYM("___fpclassify", d___fpclassifyd);
DSYM("___exp10", exp10);
DSYM("___exp10f", exp10f);

DPASS(acos);
DPASS(acosf);
DPASS(acosh);
DPASS(acoshf);
DPASS(acoshl);
DPASS(acosl);
DPASS(asin);
DPASS(asinf);
DPASS(asinh);
DPASS(asinhf);
DPASS(asinhl);
DPASS(asinl);
DPASS(atan);
DPASS(atan2);
DPASS(atan2f);
DPASS(atan2l);
DPASS(atanf);
DPASS(atanh);
DPASS(atanhf);
DPASS(atanhl);
DPASS(atanl);
DPASS(cbrt);
DPASS(cbrtf);
DPASS(cbrtl);
DPASS(ceil);
DPASS(ceilf);
DPASS(ceill);
DPASS(copysign);
DPASS(copysignf);
DPASS(copysignl);
DPASS(cos);
DPASS(cosf);
DPASS(cosh);
DPASS(coshf);
DPASS(coshl);
DPASS(cosl);
DPASS(erf);
DPASS(erfc);
DPASS(erfcf);
DPASS(erfcl);
DPASS(erff);
DPASS(erfl);
DPASS(exp);
DPASS(exp10);
DPASS(exp10f);
DPASS(exp10l);
DPASS(exp2);
DPASS(exp2f);
DPASS(exp2l);
DPASS(expf);
DPASS(expl);
DPASS(expm1);
DPASS(expm1f);
DPASS(expm1l);
DPASS(fabs);
DPASS(fabsf);
DPASS(fabsl);
DPASS(fdim);
DPASS(fdimf);
DPASS(fdiml);
DPASS(floor);
DPASS(floorf);
DPASS(floorl);
DPASS(fma);
DPASS(fmaf);
DPASS(fmal);
DPASS(fmax);
DPASS(fmaxf);
DPASS(fmaxl);
DPASS(fmin);
DPASS(fminf);
DPASS(fminl);
DPASS(fmod);
DPASS(fmodf);
DPASS(fmodl);
DPASS(frexp);
DPASS(frexpf);
DPASS(frexpl);
DPASS(hypot);
DPASS(hypotf);
DPASS(hypotl);
DPASS(ilogb);
DPASS(ilogbf);
DPASS(ilogbl);
DPASS(j0);
DPASS(j1);
DPASS(jn);
DPASS(ldexp);
DPASS(ldexpf);
DPASS(ldexpl);
DPASS(lgamma);
DPASS(lgammaf);
DPASS(lgammal);
DPASS(lgamma_r);
DPASS(lgammaf_r);
DPASS(llrint);
DPASS(llrintf);
DPASS(llrintl);
DPASS(llround);
DPASS(llroundf);
DPASS(llroundl);
DPASS(log);
DPASS(log10);
DPASS(log10f);
DPASS(log10l);
DPASS(log1p);
DPASS(log1pf);
DPASS(log1pl);
DPASS(log2);
DPASS(log2f);
DPASS(log2l);
DPASS(logb);
DPASS(logbf);
DPASS(logbl);
DPASS(logf);
DPASS(logl);
DPASS(lrint);
DPASS(lrintf);
DPASS(lrintl);
DPASS(lround);
DPASS(lroundf);
DPASS(lroundl);
DPASS(modf);
DPASS(modff);
DPASS(modfl);
DPASS(nan);
DPASS(nanf);
DPASS(nanl);
DPASS(nearbyint);
DPASS(nearbyintf);
DPASS(nearbyintl);
DPASS(nextafter);
DPASS(nextafterf);
DPASS(nextafterl);
DPASS(nexttoward);
DPASS(nexttowardf);
DPASS(nexttowardl);
DPASS(pow);
DPASS(powf);
DPASS(powl);
DPASS(remainder);
DPASS(remainderf);
DPASS(remainderl);
DPASS(remquo);
DPASS(remquof);
DPASS(remquol);
DPASS(rint);
DPASS(rintf);
DPASS(rintl);
DPASS(round);
DPASS(roundf);
DPASS(roundl);
DPASS(scalb);
DPASS(scalbln);
DPASS(scalblnf);
DPASS(scalblnl);
DPASS(scalbn);
DPASS(scalbnf);
DPASS(scalbnl);
DPASS(significand);
DPASS(sin);
DPASS(sinf);
DPASS(sinh);
DPASS(sinhf);
DPASS(sinhl);
DPASS(sinl);
DPASS(sqrt);
DPASS(sqrtf);
DPASS(sqrtl);
DPASS(tan);
DPASS(tanf);
DPASS(tanh);
DPASS(tanhf);
DPASS(tanhl);
DPASS(tanl);
DPASS(tgamma);
DPASS(tgammaf);
DPASS(tgammal);
DPASS(trunc);
DPASS(truncf);
DPASS(truncl);
DPASS(y0);
DPASS(y1);
DPASS(yn);
