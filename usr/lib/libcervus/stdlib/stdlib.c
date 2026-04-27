#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <sys/syscall.h>

extern int __cervus_errno;
extern int    __cervus_argc;
extern char **__cervus_argv;

int abs(int x)              { return x < 0 ? -x : x; }
long labs(long x)           { return x < 0 ? -x : x; }
long long llabs(long long x){ return x < 0 ? -x : x; }

static long long __parse_signed(const char *s, char **end, int base, int is_unsigned)
{
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    } else if (base == 0 && *s == '0') {
        s++; base = 8;
    } else if (base == 0) {
        base = 10;
    }
    unsigned long long v = 0;
    while (*s) {
        int d;
        if (isdigit((unsigned char)*s)) d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long long)base + (unsigned long long)d;
        s++;
    }
    if (end) *end = (char *)s;
    if (is_unsigned) return (long long)v;
    return neg ? -(long long)v : (long long)v;
}

long          strtol(const char *s, char **e, int b)    { return (long)__parse_signed(s, e, b, 0); }
long long     strtoll(const char *s, char **e, int b)   { return __parse_signed(s, e, b, 0); }
unsigned long strtoul(const char *s, char **e, int b)   { return (unsigned long)__parse_signed(s, e, b, 1); }
unsigned long long strtoull(const char *s, char **e, int b) { return (unsigned long long)__parse_signed(s, e, b, 1); }

int       atoi(const char *s)  { return (int)strtol(s, NULL, 10); }
long      atol(const char *s)  { return strtol(s, NULL, 10); }
long long atoll(const char *s) { return strtoll(s, NULL, 10); }

static unsigned long __rand_state = 1;
int  rand(void)               { __rand_state = __rand_state * 1103515245UL + 12345UL; return (int)((__rand_state >> 16) & 0x7FFF); }
void srand(unsigned int seed)  { __rand_state = seed; }

static void __qswap(void *a, void *b, size_t sz)
{
    unsigned char tmp;
    unsigned char *pa = (unsigned char *)a;
    unsigned char *pb = (unsigned char *)b;
    while (sz--) { tmp = *pa; *pa++ = *pb; *pb++ = tmp; }
}

void qsort(void *base, size_t nmemb, size_t sz, int (*cmp)(const void *, const void *))
{
    if (nmemb < 2) return;
    unsigned char *arr = (unsigned char *)base;
    unsigned char *pivot = arr + (nmemb - 1) * sz;
    size_t i = 0;
    for (size_t j = 0; j < nmemb - 1; j++) {
        if (cmp(arr + j * sz, pivot) <= 0) {
            if (i != j) __qswap(arr + i * sz, arr + j * sz, sz);
            i++;
        }
    }
    __qswap(arr + i * sz, pivot, sz);
    qsort(arr, i, sz, cmp);
    qsort(arr + (i + 1) * sz, nmemb - i - 1, sz, cmp);
}

char *getenv(const char *name)
{
    if (!name) return NULL;
    size_t nl = strlen(name);
    for (int i = 1; i < __cervus_argc; i++) {
        const char *a = __cervus_argv[i];
        if (a && a[0] == '-' && a[1] == '-' &&
            a[2] == 'e' && a[3] == 'n' && a[4] == 'v' && a[5] == ':') {
            const char *kv = a + 6;
            if (strncmp(kv, name, nl) == 0 && kv[nl] == '=')
                return (char *)(kv + nl + 1);
        }
    }
    return NULL;
}

int system(const char *cmd)
{
    (void)cmd;
    __cervus_errno = ENOSYS;
    return -1;
}

#define ATEXIT_MAX 32
static void (*__atexit_fns[ATEXIT_MAX])(void);
static int __atexit_cnt = 0;

int atexit(void (*fn)(void))
{
    if (__atexit_cnt >= ATEXIT_MAX) return -1;
    __atexit_fns[__atexit_cnt++] = fn;
    return 0;
}

int fflush(FILE *stream);

void exit(int status)
{
    while (__atexit_cnt > 0) {
        __atexit_cnt--;
        if (__atexit_fns[__atexit_cnt]) __atexit_fns[__atexit_cnt]();
    }
    extern FILE *stdout;
    extern FILE *stderr;
    fflush(stdout);
    fflush(stderr);
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

void abort(void)
{
    syscall1(SYS_EXIT, 134);
    __builtin_unreachable();
}