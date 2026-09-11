#include <math.h>

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 1.0) return 1.0;
    if (isnan(x) || isnan(y)) return NAN;

    if (y == (double)(long)y) {
        long n = (long)y;
        if (n > -64 && n < 64) {
            double r = 1.0, b = (n < 0) ? 1.0 / x : x;
            long k = (n < 0) ? -n : n;
            while (k--) r *= b;
            return r;
        }
    }

    if (x == 0.0) return (y > 0.0) ? 0.0 : INFINITY;
    if (x < 0.0)  return NAN;

    extern double cv_log2_impl(double);
    return exp2(y * cv_log2_impl(x));
}
