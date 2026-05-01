#include <math.h>
#include <stdint.h>

double fabs(double x) {
    union {
        double d;
        uint64_t i;
    } u = { .d = x };
    
    u.i &= 0x7FFFFFFFFFFFFFFFULL;
    return u.d;
}