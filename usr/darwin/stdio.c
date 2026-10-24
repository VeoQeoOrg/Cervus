#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <wchar.h>
#include <sys/types.h>
#include "darwin.h"

#define DSF_RW  0x0010
#define DSF_EOF 0x0020
#define DSF_ERR 0x0040

_Static_assert(sizeof(struct dfile) == 152, "Darwin FILE is 152 bytes");
_Static_assert(offsetof(struct dfile, lbfsize) == 40, "Darwin FILE layout");
_Static_assert(offsetof(struct dfile, offset) == 144, "Darwin FILE layout");

static struct dfile d_sF[3];
static void *d_stdinp = &d_sF[0];
static void *d_stdoutp = &d_sF[1];
static void *d_stderrp = &d_sF[2];

DSYM("___sF", d_sF);
DSYM("___stdinp", &d_stdinp);
DSYM("___stdoutp", &d_stdoutp);
DSYM("___stderrp", &d_stderrp);

static void sync_flags(struct dfile *f)
{
    short fl = DSF_RW;
    if (f->real && feof(f->real))
        fl |= DSF_EOF;
    if (f->real && ferror(f->real))
        fl |= DSF_ERR;
    f->flags = fl;
    f->r = 0;
    f->w = 0;
}

static void attach(struct dfile *f, FILE *real)
{
    f->real = real;
    f->file = (short)(real ? fileno(real) : -1);
    f->p = f->nbuf;
    sync_flags(f);
}

void d_stdio_init(void)
{
    attach(&d_sF[0], stdin);
    attach(&d_sF[1], stdout);
    attach(&d_sF[2], stderr);
}

FILE *d_real_file(void *p)
{
    return p ? ((struct dfile *)p)->real : NULL;
}

static int is_std(struct dfile *f)
{
    return f >= d_sF && f < d_sF + 3;
}

static struct dfile *wrap(FILE *real)
{
    if (!real)
        return NULL;
    struct dfile *f = calloc(1, sizeof *f);
    if (!f) {
        fclose(real);
        errno = ENOMEM;
        return NULL;
    }
    attach(f, real);
    return f;
}

#define R(f) ((f)->real)

static struct dfile *d_fopen(const char *p, const char *m) { return wrap(fopen(p, m)); }
static struct dfile *d_fdopen(int fd, const char *m) { return wrap(fdopen(fd, m)); }
static struct dfile *d_popen(const char *c, const char *m) { return wrap(popen(c, m)); }
static struct dfile *d_tmpfile(void) { return wrap(tmpfile()); }
static struct dfile *d_open_memstream(char **b, size_t *s) { return wrap(open_memstream(b, s)); }
static struct dfile *d_fmemopen(void *b, size_t s, const char *m) { return wrap(fmemopen(b, s, m)); }

static struct dfile *d_freopen(const char *p, const char *m, struct dfile *f)
{
    FILE *r = freopen(p, m, R(f));
    if (!r) {
        if (!is_std(f))
            free(f);
        return NULL;
    }
    attach(f, r);
    return f;
}

static int d_fclose(struct dfile *f)
{
    int r = fclose(R(f));
    if (is_std(f))
        attach(f, NULL);
    else
        free(f);
    return r;
}

static int d_pclose(struct dfile *f)
{
    int r = pclose(R(f));
    if (!is_std(f))
        free(f);
    return r > 0 ? d_wstatus(r) : r;
}

struct dcookie {
    void *cookie;
    int (*rd)(void *, char *, int);
    int (*wr)(void *, const char *, int);
    int64_t (*sk)(void *, int64_t, int);
    int (*cl)(void *);
};

static ssize_t ck_read(void *c, char *b, size_t n)
{
    struct dcookie *k = c;
    return k->rd(k->cookie, b, n > 0x7fffffff ? 0x7fffffff : (int)n);
}

static ssize_t ck_write(void *c, const char *b, size_t n)
{
    struct dcookie *k = c;
    return k->wr(k->cookie, b, n > 0x7fffffff ? 0x7fffffff : (int)n);
}

static int ck_seek(void *c, off64_t *off, int wh)
{
    struct dcookie *k = c;
    int64_t r = k->sk(k->cookie, *off, wh);
    if (r < 0)
        return -1;
    *off = r;
    return 0;
}

static int ck_close(void *c)
{
    struct dcookie *k = c;
    int r = k->cl ? k->cl(k->cookie) : 0;
    free(k);
    return r;
}

static struct dfile *d_funopen(void *cookie, int (*rd)(void *, char *, int),
                               int (*wr)(void *, const char *, int),
                               int64_t (*sk)(void *, int64_t, int), int (*cl)(void *))
{
    struct dcookie *k = malloc(sizeof *k);
    if (!k) {
        errno = ENOMEM;
        return NULL;
    }
    k->cookie = cookie;
    k->rd = rd;
    k->wr = wr;
    k->sk = sk;
    k->cl = cl;
    cookie_io_functions_t io = {
        rd ? ck_read : NULL, wr ? ck_write : NULL, sk ? ck_seek : NULL, ck_close,
    };
    FILE *r = fopencookie(k, rd && wr ? "r+" : wr ? "w" : "r", io);
    if (!r) {
        free(k);
        return NULL;
    }
    return wrap(r);
}

static int d_fflush(struct dfile *f) { return fflush(f ? R(f) : NULL); }
static int d_fileno(struct dfile *f) { return fileno(R(f)); }
static int d_feof(struct dfile *f) { return feof(R(f)); }
static int d_ferror(struct dfile *f) { return ferror(R(f)); }
static void d_clearerr(struct dfile *f) { clearerr(R(f)); sync_flags(f); }
static int d_fpurge(struct dfile *f) { int r = fpurge(R(f)); sync_flags(f); return r; }
static void d_flockfile(struct dfile *f) { flockfile(R(f)); }
static void d_funlockfile(struct dfile *f) { funlockfile(R(f)); }
static int d_ftrylockfile(struct dfile *f) { return ftrylockfile(R(f)); }

static int d_fgetc(struct dfile *f)
{
    int c = fgetc(R(f));
    if (c == EOF)
        sync_flags(f);
    return c;
}

static int d_fputc(int c, struct dfile *f)
{
    int r = fputc(c, R(f));
    if (r == EOF)
        sync_flags(f);
    return r;
}

static int d___srget(struct dfile *f)
{
    f->r = 0;
    return d_fgetc(f);
}

static int d___swbuf(int c, struct dfile *f)
{
    f->w = 0;
    return d_fputc(c, f);
}

static int d_ungetc(int c, struct dfile *f)
{
    int r = ungetc(c, R(f));
    sync_flags(f);
    return r;
}

static char *d_fgets(char *s, int n, struct dfile *f)
{
    char *r = fgets(s, n, R(f));
    if (!r)
        sync_flags(f);
    return r;
}

static int d_fputs(const char *s, struct dfile *f) { return fputs(s, R(f)); }

static size_t d_fread(void *p, size_t sz, size_t n, struct dfile *f)
{
    size_t r = fread(p, sz, n, R(f));
    if (r < n)
        sync_flags(f);
    return r;
}

static size_t d_fwrite(const void *p, size_t sz, size_t n, struct dfile *f)
{
    size_t r = fwrite(p, sz, n, R(f));
    if (r < n)
        sync_flags(f);
    return r;
}

static ssize_t d_getdelim(char **l, size_t *n, int d, struct dfile *f)
{
    ssize_t r = getdelim(l, n, d, R(f));
    if (r < 0)
        sync_flags(f);
    return r;
}

static ssize_t d_getline(char **l, size_t *n, struct dfile *f) { return d_getdelim(l, n, '\n', f); }

static char *d_fgetln(struct dfile *f, size_t *len)
{
    char *r = fgetln(R(f), len);
    if (!r)
        sync_flags(f);
    return r;
}

static int d_vfprintf(struct dfile *f, const char *fmt, va_list ap) { return vfprintf(R(f), fmt, ap); }

static int d_fprintf(struct dfile *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(R(f), fmt, ap);
    va_end(ap);
    return r;
}

static int d_vfscanf(struct dfile *f, const char *fmt, va_list ap)
{
    int r = vfscanf(R(f), fmt, ap);
    sync_flags(f);
    return r;
}

static int d_fscanf(struct dfile *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = d_vfscanf(f, fmt, ap);
    va_end(ap);
    return r;
}

static int d_fseeko(struct dfile *f, off_t o, int wh)
{
    int r = fseeko(R(f), o, wh);
    sync_flags(f);
    return r;
}

static int d_fseek(struct dfile *f, long o, int wh) { return d_fseeko(f, o, wh); }
static off_t d_ftello(struct dfile *f) { return ftello(R(f)); }
static long d_ftell(struct dfile *f) { return (long)ftello(R(f)); }
static void d_rewind(struct dfile *f) { rewind(R(f)); sync_flags(f); }

static int d_fgetpos(struct dfile *f, int64_t *pos)
{
    off_t o = ftello(R(f));
    if (o < 0)
        return -1;
    *pos = o;
    return 0;
}

static int d_fsetpos(struct dfile *f, const int64_t *pos) { return d_fseeko(f, *pos, SEEK_SET); }

static int d_setvbuf(struct dfile *f, char *b, int mode, size_t sz) { return setvbuf(R(f), b, mode, sz); }
static void d_setbuf(struct dfile *f, char *b) { setvbuf(R(f), b, b ? _IOFBF : _IONBF, BUFSIZ); }
static void d_setbuffer(struct dfile *f, char *b, int sz) { setvbuf(R(f), b, b ? _IOFBF : _IONBF, (size_t)sz); }
static int d_setlinebuf(struct dfile *f) { return setvbuf(R(f), NULL, _IOLBF, 0); }

static wint_t d_fgetwc(struct dfile *f)
{
    wint_t c = fgetwc(R(f));
    if (c == WEOF)
        sync_flags(f);
    return c;
}

static wint_t d_fputwc(wchar_t c, struct dfile *f) { return fputwc(c, R(f)); }
static wint_t d_ungetwc(wint_t c, struct dfile *f) { wint_t r = ungetwc(c, R(f)); sync_flags(f); return r; }

static wchar_t *d_fgetws(wchar_t *s, int n, struct dfile *f)
{
    wchar_t *r = fgetws(s, n, R(f));
    if (!r)
        sync_flags(f);
    return r;
}

static int d_fputws(const wchar_t *s, struct dfile *f) { return fputws(s, R(f)); }
static int d_fwide(struct dfile *f, int m) { return fwide(R(f), m); }
static int d_vfwprintf(struct dfile *f, const wchar_t *fmt, va_list ap) { return vfwprintf(R(f), fmt, ap); }

static int d_fwprintf(struct dfile *f, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfwprintf(R(f), fmt, ap);
    va_end(ap);
    return r;
}

DSYM("_fopen", d_fopen);
DSYM("_fdopen", d_fdopen);
DSYM("_freopen", d_freopen);
DSYM("_popen", d_popen);
DSYM("_pclose", d_pclose);
DSYM("_tmpfile", d_tmpfile);
DSYM("_open_memstream", d_open_memstream);
DSYM("_fmemopen", d_fmemopen);
DSYM("_funopen", d_funopen);
DSYM("_fclose", d_fclose);
DSYM("_fflush", d_fflush);
DSYM("_fileno", d_fileno);
DSYM("_feof", d_feof);
DSYM("_ferror", d_ferror);
DSYM("_clearerr", d_clearerr);
DSYM("_fpurge", d_fpurge);
DSYM("_flockfile", d_flockfile);
DSYM("_funlockfile", d_funlockfile);
DSYM("_ftrylockfile", d_ftrylockfile);
DSYM("_fgetc", d_fgetc);
DSYM("_getc", d_fgetc);
DSYM("_getc_unlocked", d_fgetc);
DSYM("_fputc", d_fputc);
DSYM("_putc", d_fputc);
DSYM("_putc_unlocked", d_fputc);
DSYM("___srget", d___srget);
DSYM("___swbuf", d___swbuf);
DSYM("_ungetc", d_ungetc);
DSYM("_fgets", d_fgets);
DSYM("_fputs", d_fputs);
DSYM("_fread", d_fread);
DSYM("_fwrite", d_fwrite);
DSYM("_getline", d_getline);
DSYM("_getdelim", d_getdelim);
DSYM("_fgetln", d_fgetln);
DSYM("_fprintf", d_fprintf);
DSYM("_vfprintf", d_vfprintf);
DSYM("_fscanf", d_fscanf);
DSYM("_vfscanf", d_vfscanf);
DSYM("_fseek", d_fseek);
DSYM("_fseeko", d_fseeko);
DSYM("_ftell", d_ftell);
DSYM("_ftello", d_ftello);
DSYM("_rewind", d_rewind);
DSYM("_fgetpos", d_fgetpos);
DSYM("_fsetpos", d_fsetpos);
DSYM("_setvbuf", d_setvbuf);
DSYM("_setbuf", d_setbuf);
DSYM("_setbuffer", d_setbuffer);
DSYM("_setlinebuf", d_setlinebuf);
DSYM("_fgetwc", d_fgetwc);
DSYM("_getwc", d_fgetwc);
DSYM("_fputwc", d_fputwc);
DSYM("_putwc", d_fputwc);
DSYM("_ungetwc", d_ungetwc);
DSYM("_fgetws", d_fgetws);
DSYM("_fputws", d_fputws);
DSYM("_fwide", d_fwide);
DSYM("_fwprintf", d_fwprintf);
DSYM("_vfwprintf", d_vfwprintf);

DPASS(printf);
DPASS(vprintf);
DPASS(sprintf);
DPASS(vsprintf);
DPASS(snprintf);
DPASS(vsnprintf);
DPASS(asprintf);
DPASS(vasprintf);
DPASS(dprintf);
DPASS(vdprintf);
DPASS(sscanf);
DPASS(vsscanf);
DPASS(scanf);
DPASS(vscanf);
DPASS(puts);
DPASS(putchar);
DPASS(getchar);
DPASS(remove);
DPASS(rename);
DPASS(renameat);
DPASS(tmpnam);
DPASS(tempnam);
DPASS(wprintf);
DPASS(swprintf);
DPASS(vswprintf);
DPASS(putwchar);
DPASS(getwchar);
