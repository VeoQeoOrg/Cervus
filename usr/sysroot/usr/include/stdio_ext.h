#ifndef _STDIO_EXT_H
#define _STDIO_EXT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#define FSETLOCKING_QUERY    0
#define FSETLOCKING_INTERNAL 1
#define FSETLOCKING_BYCALLER 2

size_t      __fbufsize(FILE *f);
size_t      __fpending(FILE *f);
int         __flbf(FILE *f);
int         __freadable(FILE *f);
int         __fwritable(FILE *f);
int         __freading(FILE *f);
int         __fwriting(FILE *f);
int         __fsetlocking(FILE *f, int type);
void        __fpurge(FILE *f);
void        _flushlbf(void);
void        __fseterr(FILE *f);
size_t      __freadahead(FILE *f);
const char *__freadptr(FILE *f, size_t *sizep);
void        __freadptrinc(FILE *f, size_t inc);
int         fpurge(FILE *f);

#ifdef __cplusplus
}
#endif
#endif
