#include <string.h>

void* memset_explicit(void* dst, int c, size_t n) {
    volatile unsigned char* p = (volatile unsigned char*)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}