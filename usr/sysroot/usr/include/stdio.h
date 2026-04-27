#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)

#define BUFSIZ 1024

typedef struct __cervus_FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int putchar(int c);
int getchar(void);
int puts(const char *s);
int fputs(const char *s, FILE *stream);
int fputc(int c, FILE *stream);
int fgetc(FILE *stream);
char *fgets(char *s, int n, FILE *stream);

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int snprintf(char *buf, size_t sz, const char *fmt, ...);
int vsnprintf(char *buf, size_t sz, const char *fmt, va_list ap);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int vprintf(const char *fmt, va_list ap);

FILE  *fopen(const char *path, const char *mode);
FILE  *tmpfile(void);
int    fclose(FILE *stream);
size_t fread(void *buf, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *stream);
int    fflush(FILE *stream);
int    fseek(FILE *stream, long off, int whence);
long   ftell(FILE *stream);
int    feof(FILE *stream);
int    ferror(FILE *stream);
void   clearerr(FILE *stream);
int    fileno(FILE *stream);

void perror(const char *msg);
int  rename(const char *oldp, const char *newp);
int  remove(const char *path);

#endif