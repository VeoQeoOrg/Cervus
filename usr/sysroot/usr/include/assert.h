#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef NDEBUG
#define assert(cond) ((void)0)
#else

void __cervus_assert_fail(const char *expr, const char *file, int line, const char *func)
    __attribute__((noreturn));

#define assert(cond) \
    ((cond) ? (void)0 : __cervus_assert_fail(#cond, __FILE__, __LINE__, __func__))

#endif

#endif