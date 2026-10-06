#include <string.h>

void explicit_bzero(void *p, size_t n)
{
    memset(p, 0, n);
    __asm__ volatile("" : : "r"(p) : "memory");
}
