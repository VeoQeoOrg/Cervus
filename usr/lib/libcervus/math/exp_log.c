#include <math.h>
#include <stdint.h>

#define LN2      0.69314718055994530942
#define LOG2E    1.44269504088896340736
#define LOG10_2  0.30102999566398119521

static double exp_small(double y) {
    double term = 1.0, sum = 1.0;
    for (int k = 1; k <= 12; k++) {
        term *= y / (double)k;
        sum += term;
    }
    return sum;
}

double exp2(double x) {
    if (isnan(x)) return x;
    if (x > 1024.0)  return INFINITY;
    if (x < -1075.0) return 0.0;

    double i = floor(x);
    double f = x - i;

    double r = exp_small((f / 8.0) * LN2);
    r *= r; r *= r; r *= r;

    return ldexp(r, (int)i);
}

double cv_log2_impl(double x);

double cv_log2_impl(double x) {
    if (isnan(x)) return x;
    if (x < 0.0)  return NAN;
    if (x == 0.0) return -INFINITY;
    if (isinf(x)) return x;

    union { double d; uint64_t u; } v;
    v.d = x;
    int e = (int)((v.u >> 52) & 0x7FF) - 1023;
    if (e == -1023) {
        v.d = x * 9007199254740992.0;
        e = (int)((v.u >> 52) & 0x7FF) - 1023 - 53;
    }
    v.u = (v.u & 0x000FFFFFFFFFFFFFull) | ((uint64_t)1023 << 52);
    double m = v.d;

    double z = (m - 1.0) / (m + 1.0);
    double z2 = z * z;
    double sum = 0.0, zp = z;
    for (int k = 1; k <= 21; k += 2) {
        sum += zp / (double)k;
        zp *= z2;
    }
    return (2.0 * sum) / LN2 + (double)e;
}

double exp(double x) {
    return exp2(x * LOG2E);
}

double log(double x) {
    if (x < 0.0)  return NAN;
    if (x == 0.0) return -INFINITY;
    return cv_log2_impl(x) * LN2;
}

double log10(double x) {
    if (x < 0.0)  return NAN;
    if (x == 0.0) return -INFINITY;
    return cv_log2_impl(x) * LOG10_2;
}

double fmod(double x, double y) {
    if (y == 0.0 || isnan(x) || isnan(y) || isinf(x)) return NAN;
    if (isinf(y)) return x;
    if (x == 0.0) return x;

    double ax = fabs(x), ay = fabs(y);
    if (ax < ay) return x;

    double q = trunc(ax / ay);
    double r = ax - q * ay;
    if (r < 0.0) r = 0.0;
    if (r >= ay) r = r - ay;
    return (x < 0.0) ? -r : r;
}

double copysign(double x, double y) {
    union { double d; uint64_t u; } a, b;
    a.d = x; b.d = y;
    a.u = (a.u & 0x7FFFFFFFFFFFFFFFull) | (b.u & 0x8000000000000000ull);
    return a.d;
}

double trunc(double x) {
    return (x < 0.0) ? ceil(x) : floor(x);
}

double fmin(double a, double b) {
    if (isnan(a)) return b;
    if (isnan(b)) return a;
    return a < b ? a : b;
}

double fmax(double a, double b) {
    if (isnan(a)) return b;
    if (isnan(b)) return a;
    return a > b ? a : b;
}

double hypot(double x, double y) {
    x = fabs(x);
    y = fabs(y);
    if (x < y) { double t = x; x = y; y = t; }
    if (x == 0.0) return 0.0;
    if (isinf(x) || isinf(y)) return INFINITY;
    double r = y / x;
    return x * sqrt(1.0 + r * r);
}

double cbrt(double x) {
    if (x == 0.0 || isnan(x) || isinf(x)) return x;
    double s = (x < 0.0) ? -1.0 : 1.0;
    double a = fabs(x);
    double r = exp2(cv_log2_impl(a) / 3.0);
    for (int i = 0; i < 3; i++)
        r = r - (r * r * r - a) / (3.0 * r * r);
    return s * r;
}

double atan(double x) {
    return atan2(x, 1.0);
}

double asin(double x) {
    if (x < -1.0 || x > 1.0) return NAN;
    if (x == 1.0)  return M_PI / 2.0;
    if (x == -1.0) return -M_PI / 2.0;
    return atan2(x, sqrt(1.0 - x * x));
}

double acos(double x) {
    if (x < -1.0 || x > 1.0) return NAN;
    return atan2(sqrt(1.0 - x * x), x);
}

double sinh(double x) {
    if (isnan(x) || isinf(x)) return x;
    double e = exp(x);
    return (e - 1.0 / e) / 2.0;
}

double cosh(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return INFINITY;
    double e = exp(x);
    return (e + 1.0 / e) / 2.0;
}

double tanh(double x) {
    if (isnan(x)) return x;
    if (x >  20.0) return 1.0;
    if (x < -20.0) return -1.0;
    double e = exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}
