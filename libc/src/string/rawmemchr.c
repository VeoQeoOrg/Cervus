#include <string.h>

void *rawmemchr(void *ptr, int val) {
    const unsigned char *p = (const unsigned char*)ptr;
    unsigned char c = (unsigned char)val;
    for (;;) {
        if (*p == c)
            return (void*)p;
        p++;
    }
}
