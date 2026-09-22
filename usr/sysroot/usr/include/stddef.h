#ifndef _STDDEF_H
#define _STDDEF_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef unsigned long size_t;
#ifndef __ssize_t_defined
#define __ssize_t_defined
typedef long          ssize_t;
#endif
typedef long          ptrdiff_t;
typedef int           wchar_t;

#define offsetof(t, m) __builtin_offsetof(t, m)

#ifdef __cplusplus
}
#endif
#endif