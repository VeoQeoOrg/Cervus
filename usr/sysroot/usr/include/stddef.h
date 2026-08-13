#ifndef _STDDEF_H
#define _STDDEF_H

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

#endif