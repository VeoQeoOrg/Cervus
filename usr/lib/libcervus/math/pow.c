#include <math.h>

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    
    if (y == (int)y) {
        int n = (int)y;
        double result = 1.0;
        
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                result *= x;
            }
        } else {
            for (int i = 0; i < -n; i++) {
                result /= x;
            }
        }
        
        return result;
    }
    
    if (x > 0.0) {
        int int_part = (int)y;
        double frac_part = y - int_part;
        
        double int_pow = pow(x, int_part);
        
        if (frac_part > 0.0) {
            return int_pow * (1.0 + frac_part * (x - 1.0));
        } else {
            return int_pow / (1.0 - frac_part * (x - 1.0));
        }
    }
    
    return NAN;
}