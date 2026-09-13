#include <math.h>
#include <stdint.h>

double frexp(double x, int *e)
{
    union { double d; uint64_t u; } v;
    v.d = x;
    int exp = (int)((v.u >> 52) & 0x7FF);

    if (exp == 0x7FF || x == 0.0) { *e = 0; return x; }

    if (exp == 0) {
        v.d = x * 18014398509481984.0;
        exp = (int)((v.u >> 52) & 0x7FF) - 54;
    }

    *e = exp - 1022;
    v.u = (v.u & ~((uint64_t)0x7FF << 52)) | ((uint64_t)1022 << 52);
    return v.d;
}

double modf(double x, double *ip)
{
    if (isnan(x)) { *ip = x; return x; }
    if (isinf(x)) { *ip = x; return copysign(0.0, x); }
    double i = trunc(x);
    *ip = i;
    return x - i;
}

double scalbn(double x, int n)
{
    return ldexp(x, n);
}

double rint(double x)
{
    if (isnan(x) || isinf(x)) return x;
    double i = trunc(x);
    double f = x - i;
    double a = f < 0 ? -f : f;
    if (a > 0.5) return i + (x < 0 ? -1.0 : 1.0);
    if (a < 0.5) return i;
    double half = i * 0.5;
    if (half == trunc(half)) return i;
    return i + (x < 0 ? -1.0 : 1.0);
}

double nearbyint(double x) { return rint(x); }

long      lround(double x)  { return (long)round(x); }
long long llround(double x) { return (long long)round(x); }
long long llrint(double x)  { return (long long)rint(x); }

double fdim(double x, double y)
{
    if (isnan(x) || isnan(y)) return NAN;
    return x > y ? x - y : 0.0;
}

double fma(double x, double y, double z) { return x * y + z; }

double log1p(double x)
{
    if (x <= -1.0) return x == -1.0 ? -INFINITY : NAN;
    double u = 1.0 + x;
    if (u == 1.0) return x;
    return log(u) * (x / (u - 1.0));
}

double expm1(double x)
{
    double u = exp(x);
    if (u == 1.0) return x;
    if (isinf(u) || isnan(u)) return u;
    if (u - 1.0 == -1.0) return -1.0;
    return (u - 1.0) * x / log(u);
}

double asinh(double x)
{
    double a = x < 0 ? -x : x;
    double r;
    if (a < 1e-8) r = a;
    else if (a < 2.0) r = log1p(a + a * a / (1.0 + sqrt(1.0 + a * a)));
    else if (a > 1e150) r = log(a) + 0.69314718055994530942;
    else r = log(2.0 * a + 1.0 / (sqrt(a * a + 1.0) + a));
    return x < 0 ? -r : r;
}

double acosh(double x)
{
    if (x < 1.0) return NAN;
    if (x > 1e8) return log(x) + 0.69314718055994530942;
    return log(x + sqrt(x * x - 1.0));
}

double atanh(double x)
{
    if (x > 1.0 || x < -1.0) return NAN;
    if (x == 1.0) return INFINITY;
    if (x == -1.0) return -INFINITY;
    double a = x < 0 ? -x : x;
    double r = 0.5 * log1p(2.0 * a / (1.0 - a));
    return x < 0 ? -r : r;
}

double remainder(double x, double y)
{
    if (y == 0.0 || isnan(x) || isnan(y) || isinf(x)) return NAN;
    double q = rint(x / y);
    return x - q * y;
}

double nan(const char *tag)
{
    (void)tag;
    return NAN;
}

float powf(float x, float y)        { return (float)pow((double)x, (double)y); }
float expf(float x)                 { return (float)exp((double)x); }
float exp2f(float x)                { return (float)exp2((double)x); }
float logf(float x)                 { return (float)log((double)x); }
float log2f(float x)                { return (float)log2((double)x); }
float log10f(float x)               { return (float)log10((double)x); }
float log1pf(float x)               { return (float)log1p((double)x); }
float expm1f(float x)               { return (float)expm1((double)x); }
float tanf(float x)                 { return (float)tan((double)x); }
float asinf(float x)                { return (float)asin((double)x); }
float acosf(float x)                { return (float)acos((double)x); }
float atanf(float x)                { return (float)atan((double)x); }
float atan2f(float y, float x)      { return (float)atan2((double)y, (double)x); }
float sinhf(float x)                { return (float)sinh((double)x); }
float coshf(float x)                { return (float)cosh((double)x); }
float tanhf(float x)                { return (float)tanh((double)x); }
float asinhf(float x)               { return (float)asinh((double)x); }
float acoshf(float x)               { return (float)acosh((double)x); }
float atanhf(float x)               { return (float)atanh((double)x); }
float fmodf(float x, float y)       { return (float)fmod((double)x, (double)y); }
float roundf(float x)               { return (float)round((double)x); }
float truncf(float x)               { return (float)trunc((double)x); }
float rintf(float x)                { return (float)rint((double)x); }
float nearbyintf(float x)           { return (float)rint((double)x); }
float copysignf(float x, float y)   { return (float)copysign((double)x, (double)y); }
float fminf(float a, float b)       { return (float)fmin((double)a, (double)b); }
float fmaxf(float a, float b)       { return (float)fmax((double)a, (double)b); }
float fdimf(float a, float b)       { return (float)fdim((double)a, (double)b); }
float hypotf(float x, float y)      { return (float)hypot((double)x, (double)y); }
float cbrtf(float x)                { return (float)cbrt((double)x); }
float fmaf(float x, float y, float z) { return (float)((double)x * (double)y + (double)z); }
float ldexpf(float x, int e)        { return (float)ldexp((double)x, e); }
float scalbnf(float x, int e)       { return (float)ldexp((double)x, e); }
float frexpf(float x, int *e)       { return (float)frexp((double)x, e); }
float modff(float x, float *ip)
{
    double i;
    double f = modf((double)x, &i);
    *ip = (float)i;
    return (float)f;
}
