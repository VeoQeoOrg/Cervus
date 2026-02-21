#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

char *itoa(int val, char *restrict str, int base) {
    int i = 0;
    bool negative = false;

    if (val == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }

    if (val < 0 && base == 10) {
        negative = true;
        val = -val;
    }

    while (val != 0) {
        int rem = val % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        val = val / base;
    }

    if (negative)
        str[i++] = '-';

    str[i] = '\0';

    int start = 0, end = strlen(str) - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        end--;
        start++;
    }

    return str;
}
