#ifndef _STDDEF_H
#define _STDDEF_H

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef unsigned long size_t;
typedef long          ssize_t;
typedef long          ptrdiff_t;
typedef int           wchar_t;

#define offsetof(t, m) __builtin_offsetof(t, m)

#endif
