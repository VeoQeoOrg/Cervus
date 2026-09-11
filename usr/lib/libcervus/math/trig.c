#include <math.h>

static const double HALF_PI = 1.5707963267948966;
static const double TWO_PI  = 6.283185307179586;

static double sin_poly(double r) {
    double r2 = r * r;
    return r * (1.0 + r2 * (-1.0/6.0 + r2 * (1.0/120.0 + r2 * (-1.0/5040.0
             + r2 * (1.0/362880.0 + r2 * (-1.0/39916800.0
             + r2 * (1.0/6227020800.0 + r2 * (-1.0/1307674368000.0))))))));
}

static double cos_poly(double r) {
    double r2 = r * r;
    return 1.0 + r2 * (-0.5 + r2 * (1.0/24.0 + r2 * (-1.0/720.0
         + r2 * (1.0/40320.0 + r2 * (-1.0/3628800.0
         + r2 * (1.0/479001600.0 + r2 * (-1.0/87178291200.0)))))));
}

double cos(double x) {
    if (x < 0) x = -x;
    if (x >= TWO_PI) x -= TWO_PI * (double)(int64_t)(x / TWO_PI);
    int64_t k = (int64_t)(x / HALF_PI + 0.5);
    double r = x - (double)k * HALF_PI;
    switch (((unsigned)k) & 3) {
        case 0:  return cos_poly(r);
        case 1:  return -sin_poly(r);
        case 2:  return -cos_poly(r);
        default: return sin_poly(r);
    }
}

double sin(double x) {
    return cos(HALF_PI - x);
}

double tan(double x) {
    double c = cos(x);
    if (c == 0.0) return 0.0;
    return sin(x) / c;
}

static double atan_one(double x) {
    int neg = 0;
    if (x < 0.0) { x = -x; neg = 1; }

    int k = 0;
    while (x > 0.05 && k < 8) {
        x = x / (1.0 + sqrt(1.0 + x * x));
        k++;
    }

    double x2 = x * x, term = x, sum = 0.0;
    for (int i = 0; i < 10; i++) {
        double t = term / (double)(2 * i + 1);
        sum += (i & 1) ? -t : t;
        term *= x2;
    }
    sum *= (double)(1 << k);
    return neg ? -sum : sum;
}

double atan2(double y, double x) {
    if (x == 0.0) {
        if (y > 0.0) return HALF_PI;
        if (y < 0.0) return -HALF_PI;
        return 0.0;
    }
    double a = atan_one(y / x);
    if (x < 0.0) return y >= 0.0 ? a + M_PI : a - M_PI;
    return a;
}
