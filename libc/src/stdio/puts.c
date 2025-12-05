#include <stdio.h>
#include <string.h>

int puts(const char *str) {
    int count = 0;
    
    if (!str) {
        const char *null_str = "(null)";
        while (*null_str) {
            putchar(*null_str++);
            count++;
        }
        putchar('\n');
        count++;
        return count;
    }
    
    while (*str) {
        putchar(*str++);
        count++;
    }
    
    putchar('\n');
    count++;
    
    return count;
}