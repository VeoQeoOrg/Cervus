#include <math.h>
#include <stdint.h>

int isnan(double x) {
    union {
        double f;
        uint64_t i;
    } u = { .f = x };

    uint64_t exp = (u.i >> 52) & 0x7FF;
    uint64_t mantissa = u.i & 0xFFFFFFFFFFFFFULL;

    return (exp == 0x7FF) && (mantissa != 0);
}