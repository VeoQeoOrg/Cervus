#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#include <stdio.h>
#include <stdlib.h>
#define assert(x) do { \
    if (!(x)) { \
        fprintf(stderr, "Assertion failed: " #x " at %s:%d\n", __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)
#endif

#endif
