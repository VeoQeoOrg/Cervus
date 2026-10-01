#include <math.h>
#include <stdint.h>
#include <string.h>

int signgam;

int ilogb(double x)
{
    if (x == 0.0) return FP_ILOGB0;
    if (isnan(x)) return FP_ILOGBNAN;
    if (isinf(x)) return 2147483647;
    int e;
    frexp(x, &e);
    return e - 1;
}

double logb(double x)
{
    if (x == 0.0) return -INFINITY;
    if (isnan(x)) return x;
    if (isinf(x)) return INFINITY;
    return (double)ilogb(x);
}

double nextafter(double x, double y)
{
    if (isnan(x) || isnan(y)) return x + y;
    if (x == y) return y;
    uint64_t u;
    if (x == 0.0) {
        u = 1;
        double r;
        memcpy(&r, &u, 8);
        return y > 0 ? r : -r;
    }
    memcpy(&u, &x, 8);
    if ((x < y) == (x > 0)) u++;
    else u--;
    double r;
    memcpy(&r, &u, 8);
    return r;
}

double nexttoward(double x, long double y) { return nextafter(x, (double)y); }

double remquo(double x, double y, int *quo)
{
    double r = remainder(x, y);
    double q = (x - r) / y;
    long long qi = (long long)(q < 0 ? q - 0.5 : q + 0.5);
    int bits = (int)((qi < 0 ? -qi : qi) & 0x7);
    *quo = qi < 0 ? -bits : bits;
    return r;
}

double scalbln(double x, long n)
{
    if (n > 100000) n = 100000;
    if (n < -100000) n = -100000;
    return scalbn(x, (int)n);
}

static const double two_over_sqrtpi = 1.12837916709551257390;
static const double inv_sqrtpi = 0.56418958354775628695;

static double erf_series(double x)
{
    double x2 = x * x, term = x, sum = x;
    for (int n = 1; n < 200; n++) {
        term *= -x2 / n;
        double add = term / (2 * n + 1);
        sum += add;
        if (fabs(add) < 1e-17 * fabs(sum)) break;
    }
    return two_over_sqrtpi * sum;
}

static double erfc_cf(double x)
{
    double tiny = 1e-300, f = x, c = x, d = 0.0;
    for (int n = 1; n < 500; n++) {
        double a = n * 0.5;
        d = x + a * d;
        if (fabs(d) < tiny) d = tiny;
        c = x + a / c;
        if (fabs(c) < tiny) c = tiny;
        d = 1.0 / d;
        double delta = c * d;
        f *= delta;
        if (fabs(delta - 1.0) < 1e-16) break;
    }
    return exp(-x * x) * inv_sqrtpi / f;
}

double erfc(double x)
{
    if (isnan(x)) return x;
    if (x < 0) return 2.0 - erfc(-x);
    if (x < 1.5) return 1.0 - erf_series(x);
    if (x > 27.3) return 0.0;
    return erfc_cf(x);
}

double erf(double x)
{
    if (isnan(x)) return x;
    if (x < 0) return -erf(-x);
    if (x < 2.5) return erf_series(x);
    if (x > 6.0) return 1.0;
    return 1.0 - erfc_cf(x);
}

static const double lanczos_g = 7.0;
static const double lanczos_c[9] = {
    0.99999999999980993, 676.5203681218851, -1259.1392167224028,
    771.32342877765313, -176.61502916214059, 12.507343278686905,
    -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7,
};
static const double half_log_2pi = 0.91893853320467274178;
static const double pi_const = 3.14159265358979323846;

static double lgamma_pos(double x)
{
    x -= 1.0;
    double a = lanczos_c[0];
    double t = x + lanczos_g + 0.5;
    for (int i = 1; i < 9; i++) a += lanczos_c[i] / (x + i);
    return half_log_2pi + (x + 0.5) * log(t) - t + log(a);
}

double lgamma_r(double x, int *sign)
{
    *sign = 1;
    if (isnan(x)) return x;
    if (isinf(x)) return INFINITY;
    if (x <= 0 && floor(x) == x) return INFINITY;
    if (x < 0.5) {
        double s = sin(pi_const * x);
        if (s < 0) *sign = -1;
        return log(pi_const / fabs(s)) - lgamma_pos(1.0 - x);
    }
    return lgamma_pos(x);
}

double lgamma(double x) { return lgamma_r(x, &signgam); }

double tgamma(double x)
{
    if (isnan(x)) return x;
    if (x == INFINITY) return INFINITY;
    if (x == -INFINITY) return NAN;
    if (x == 0.0) return signbit(x) ? -INFINITY : INFINITY;
    if (x < 0 && floor(x) == x) return NAN;
    if (x > 171.7) return INFINITY;
    if (x == floor(x) && x <= 23) {
        double r = 1.0;
        for (int i = 2; i < (int)x; i++) r *= i;
        return r;
    }
    int sign;
    double l = lgamma_r(x, &sign);
    return sign * exp(l);
}

int ilogbf(float x) { return ilogb(x); }
int ilogbl(long double x) { return ilogb((double)x); }
float logbf(float x) { return (float)logb(x); }
long double logbl(long double x) { return logb((double)x); }
float nextafterf(float x, float y)
{
    if (isnan(x) || isnan(y)) return x + y;
    if (x == y) return y;
    uint32_t u;
    if (x == 0.0f) {
        u = 1;
        float r;
        memcpy(&r, &u, 4);
        return y > 0 ? r : -r;
    }
    memcpy(&u, &x, 4);
    if ((x < y) == (x > 0)) u++;
    else u--;
    float r;
    memcpy(&r, &u, 4);
    return r;
}
long double nextafterl(long double x, long double y) { return nextafter((double)x, (double)y); }
float nexttowardf(float x, long double y) { return nextafterf(x, (float)y); }
long double nexttowardl(long double x, long double y) { return nextafter((double)x, (double)y); }
float remquof(float x, float y, int *quo) { return (float)remquo(x, y, quo); }
long double remquol(long double x, long double y, int *quo) { return remquo((double)x, (double)y, quo); }
float scalblnf(float x, long n) { return (float)scalbln(x, n); }
long double scalblnl(long double x, long n) { return scalbln((double)x, n); }
float erff(float x) { return (float)erf(x); }
long double erfl(long double x) { return erf((double)x); }
float erfcf(float x) { return (float)erfc(x); }
long double erfcl(long double x) { return erfc((double)x); }
float lgammaf(float x) { return (float)lgamma(x); }
long double lgammal(long double x) { return lgamma((double)x); }
float tgammaf(float x) { return (float)tgamma(x); }
long double tgammal(long double x) { return tgamma((double)x); }
