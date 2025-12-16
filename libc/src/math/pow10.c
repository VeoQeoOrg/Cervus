#include <math.h>

double pow10(int n) {
    double result = 1.0;
    
    if (n >= 0) {
        for (int i = 0; i < n; i++) {
            result *= 10.0;
        }
    } else {
        for (int i = 0; i < -n; i++) {
            result /= 10.0;
        }
    }
    
    return result;
}