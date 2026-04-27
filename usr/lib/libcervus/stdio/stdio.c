#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/cervus.h>

extern int __cervus_errno;

struct __cervus_FILE {
    int    fd;
    int    eof;
    int    err;
    int    flags;
    char  *buf;
    size_t buf_size;
    size_t buf_pos;
};

static struct __cervus_FILE __stdin_s  = { 0, 0, 0, 0, NULL, 0, 0 };
static struct __cervus_FILE __stdout_s = { 1, 0, 0, 0, NULL, 0, 0 };
static struct __cervus_FILE __stderr_s = { 2, 0, 0, 0, NULL, 0, 0 };

FILE *stdin  = &__stdin_s;
FILE *stdout = &__stdout_s;
FILE *stderr = &__stderr_s;

int fileno(FILE *s) { return s ? s->fd : -1; }
int feof(FILE *s)   { return s ? s->eof : 1; }
int ferror(FILE *s) { return s ? s->err : 1; }
void clearerr(FILE *s) { if (s) { s->eof = 0; s->err = 0; } }

FILE *fopen(const char *path, const char *mode)
{
    if (!path || !mode) return NULL;
    int flags = 0;
    int has_plus = 0;
    for (const char *m = mode + 1; *m; m++) if (*m == '+') has_plus = 1;
    switch (mode[0]) {
        case 'r': flags = has_plus ? O_RDWR : O_RDONLY; break;
        case 'w': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
        case 'a': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
        default: return NULL;
    }
    int fd = open(path, flags, 0644);
    if (fd < 0) return NULL;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }
    f->fd       = fd;
    f->eof      = 0;
    f->err      = 0;
    f->flags    = 1;
    f->buf      = NULL;
    f->buf_size = 0;
    f->buf_pos  = 0;
    return f;
}

int fclose(FILE *s)
{
    if (!s) return EOF;
    int fd = s->fd;
    int owned = s->flags & 1;
    close(fd);
    if (owned) free(s);
    return 0;
}

int fflush(FILE *s) { (void)s; return 0; }

size_t fread(void *buf, size_t size, size_t nmemb, FILE *s)
{
    if (!s || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    size_t got = 0;
    while (got < total) {
        ssize_t r = read(s->fd, (char *)buf + got, total - got);
        if (r < 0) { s->err = 1; break; }
        if (r == 0) { s->eof = 1; break; }
        got += (size_t)r;
    }
    return got / size;
}

size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *s)
{
    if (!s || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    size_t sent = 0;
    while (sent < total) {
        ssize_t w = write(s->fd, (const char *)buf + sent, total - sent);
        if (w < 0) { s->err = 1; break; }
        if (w == 0) break;
        sent += (size_t)w;
    }
    return sent / size;
}

int fseek(FILE *s, long off, int whence)
{
    if (!s) return -1;
    off_t r = lseek(s->fd, (off_t)off, whence);
    if (r == (off_t)-1) { s->err = 1; return -1; }
    s->eof = 0;
    return 0;
}

long ftell(FILE *s)
{
    if (!s) return -1;
    return (long)lseek(s->fd, 0, SEEK_CUR);
}

int fputc(int c, FILE *s)
{
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, s) != 1) return EOF;
    return (int)ch;
}

int fgetc(FILE *s)
{
    unsigned char ch;
    if (fread(&ch, 1, 1, s) != 1) return EOF;
    return (int)ch;
}

int fputs(const char *str, FILE *s)
{
    if (!str) return EOF;
    size_t n = strlen(str);
    if (fwrite(str, 1, n, s) != n) return EOF;
    return 0;
}

char *fgets(char *str, int n, FILE *s)
{
    if (!str || n <= 0 || !s) return NULL;
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(s);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        str[i++] = (char)c;
        if (c == '\n') break;
    }
    str[i] = '\0';
    return str;
}

int putchar(int c) { return fputc(c, stdout); }
int getchar(void)  { return fgetc(stdin); }

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    if (fputc('\n', stdout) == EOF) return EOF;
    return 1;
}

static void __u64_to_str(uint64_t v, char *out, int base, int upper)
{
    char tmp[32];
    int i = 0;
    const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = digs[v % (uint64_t)base]; v /= (uint64_t)base; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

int vsnprintf(char *buf, size_t sz, const char *fmt, va_list ap)
{
    size_t pos = 0;
#define __PUT(s, n) do { \
    size_t __n = (n); const char *__s = (s); \
    for (size_t __i = 0; __i < __n; __i++) { \
        if (pos + 1 < sz) buf[pos] = __s[__i]; \
        pos++; \
    } \
} while (0)

    while (*fmt) {
        if (*fmt != '%') { __PUT(fmt, 1); fmt++; continue; }
        fmt++;
        int pad_zero = 0, left_align = 0, plus_flag = 0;
        while (*fmt == '0' || *fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#') {
            if (*fmt == '0') pad_zero = 1;
            else if (*fmt == '-') left_align = 1;
            else if (*fmt == '+') plus_flag = 1;
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }
        int is_long = 0, is_size_t = 0;
        while (*fmt == 'l') { is_long++; fmt++; }
        if (*fmt == 'z') { is_size_t = 1; fmt++; }
        if (*fmt == 'h') { fmt++; }

        char nb[40];
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                size_t l = strlen(s);
                if (prec >= 0 && (size_t)prec < l) l = (size_t)prec;
                int pad = (int)(width > (int)l ? width - (int)l : 0);
                if (!left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                __PUT(s, l);
                if (left_align)  for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'd': case 'i': {
                int64_t v;
                if (is_long >= 2)   v = va_arg(ap, long long);
                else if (is_long)   v = va_arg(ap, long);
                else if (is_size_t) v = (int64_t)va_arg(ap, size_t);
                else                v = va_arg(ap, int);
                int neg = v < 0;
                uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
                __u64_to_str(u, nb, 10, 0);
                int numlen = (int)strlen(nb) + (neg || plus_flag ? 1 : 0);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align && !pad_zero) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                if (neg)           __PUT("-", 1);
                else if (plus_flag) __PUT("+", 1);
                if (!left_align && pad_zero) for (int i = 0; i < pad; i++) __PUT("0", 1);
                __PUT(nb, strlen(nb));
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'u': {
                uint64_t v;
                if (is_long >= 2)   v = va_arg(ap, unsigned long long);
                else if (is_long)   v = va_arg(ap, unsigned long);
                else if (is_size_t) v = va_arg(ap, size_t);
                else                v = va_arg(ap, unsigned);
                __u64_to_str(v, nb, 10, 0);
                int numlen = (int)strlen(nb);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align) for (int i = 0; i < pad; i++) __PUT(pad_zero ? "0" : " ", 1);
                __PUT(nb, strlen(nb));
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'x': case 'X': {
                uint64_t v;
                if (is_long >= 2)   v = va_arg(ap, unsigned long long);
                else if (is_long)   v = va_arg(ap, unsigned long);
                else if (is_size_t) v = va_arg(ap, size_t);
                else                v = va_arg(ap, unsigned);
                __u64_to_str(v, nb, 16, *fmt == 'X');
                int numlen = (int)strlen(nb);
                int pad = width > numlen ? width - numlen : 0;
                if (!left_align) for (int i = 0; i < pad; i++) __PUT(pad_zero ? "0" : " ", 1);
                __PUT(nb, strlen(nb));
                if (left_align) for (int i = 0; i < pad; i++) __PUT(" ", 1);
                break;
            }
            case 'o': {
                uint64_t v;
                if (is_long >= 2) v = va_arg(ap, unsigned long long);
                else if (is_long) v = va_arg(ap, unsigned long);
                else              v = va_arg(ap, unsigned);
                __u64_to_str(v, nb, 8, 0);
                __PUT(nb, strlen(nb));
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
                __PUT("0x", 2);
                __u64_to_str(v, nb, 16, 0);
                __PUT(nb, strlen(nb));
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                __PUT(&c, 1);
                break;
            }
            case '%': __PUT("%", 1); break;
            default: {
                char c = *fmt;
                __PUT("%", 1);
                __PUT(&c, 1);
                break;
            }
        }
        fmt++;
    }
    if (sz > 0) buf[pos < sz ? pos : sz - 1] = '\0';
#undef __PUT
    return (int)pos;
}

int snprintf(char *buf, size_t sz, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sz, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return n;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    return vsnprintf(buf, (size_t)-1, fmt, ap);
}

int vfprintf(FILE *s, const char *fmt, va_list ap)
{
    char small[512];
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(small, sizeof(small), fmt, ap2);
    va_end(ap2);
    if (needed < (int)sizeof(small)) {
        fwrite(small, 1, (size_t)needed, s);
        return needed;
    }
    char *big = (char *)malloc((size_t)needed + 1);
    if (!big) {
        fwrite(small, 1, sizeof(small) - 1, s);
        return (int)sizeof(small) - 1;
    }
    vsnprintf(big, (size_t)needed + 1, fmt, ap);
    fwrite(big, 1, (size_t)needed, s);
    free(big);
    return needed;
}

int fprintf(FILE *s, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(s, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

void perror(const char *msg)
{
    if (msg && *msg) { fputs(msg, stderr); fputs(": ", stderr); }
    fputs(strerror(__cervus_errno), stderr);
    fputc('\n', stderr);
}

int remove(const char *path) { return unlink(path); }

int mkstemp(char *template)
{
    if (!template) { __cervus_errno = EINVAL; return -1; }
    size_t len = strlen(template);
    if (len < 6) { __cervus_errno = EINVAL; return -1; }
    char *suf = template + len - 6;
    for (int i = 0; i < 6; i++) {
        if (suf[i] != 'X') { __cervus_errno = EINVAL; return -1; }
    }
    static uint64_t __mkstemp_seq = 0;
    uint64_t pid = (uint64_t)getpid();
    for (int attempt = 0; attempt < 100; attempt++) {
        uint64_t seed = (cervus_uptime_ns() ^ (pid << 32)) + (__mkstemp_seq++);
        const char *alpha = "0123456789abcdefghijklmnopqrstuvwxyz";
        for (int i = 0; i < 6; i++) {
            suf[i] = alpha[seed % 36];
            seed /= 36;
        }
        struct stat st;
        if (stat(template, &st) == 0) continue;
        int fd = open(template, O_RDWR | O_CREAT, 0600);
        if (fd >= 0) return fd;
    }
    __cervus_errno = EEXIST;
    return -1;
}

FILE *tmpfile(void)
{
    char tmpl[64];
    strcpy(tmpl, "/mnt/tmp/tmpXXXXXX");
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        strcpy(tmpl, "/tmp/tmpXXXXXX");
        fd = mkstemp(tmpl);
        if (fd < 0) return NULL;
    }
    unlink(tmpl);
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }
    f->fd       = fd;
    f->eof      = 0;
    f->err      = 0;
    f->flags    = 1;
    f->buf      = NULL;
    f->buf_size = 0;
    f->buf_pos  = 0;
    return f;
}