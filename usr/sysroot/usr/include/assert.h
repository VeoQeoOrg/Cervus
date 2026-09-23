#ifndef _ASSERT_H
#define _ASSERT_H
#ifdef __cplusplus
extern "C" {
#endif

#ifdef NDEBUG
#define assert(cond) ((void)0)
#else

void __cervus_assert_fail(const char *expr, const char *file, int line, const char *func)
    __attribute__((noreturn));

#define assert(cond) \
    ((cond) ? (void)0 : __cervus_assert_fail(#cond, __FILE__, __LINE__, __func__))

#endif

#if !defined(__cplusplus) && !defined(static_assert) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
#define static_assert _Static_assert
#endif

#ifdef __cplusplus
}
#endif
#endif