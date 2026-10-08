#include <strings.h>
#include <string.h>

void bzero(void *s, size_t n)
{
    memset(s, 0, n);
}

void bcopy(const void *src, void *dest, size_t n)
{
    memmove(dest, src, n);
}

int bcmp(const void *a, const void *b, size_t n)
{
    return memcmp(a, b, n);
}

int ffsl(long i)
{
    return i ? __builtin_ctzl((unsigned long)i) + 1 : 0;
}

int ffsll(long long i)
{
    return i ? __builtin_ctzll((unsigned long long)i) + 1 : 0;
}
