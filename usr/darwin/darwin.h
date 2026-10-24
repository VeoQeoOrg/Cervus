#ifndef DARWIN_H
#define DARWIN_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

struct dsym {
    const char *name;
    void *addr;
};

#define DSYM_CAT2(a, b) a##b
#define DSYM_CAT(a, b) DSYM_CAT2(a, b)
#define DSYM(n, a) \
    static const struct dsym DSYM_CAT(dsym_, __LINE__) \
    __attribute__((used, section("dsym"), aligned(8))) = { n, (void *)(a) }
#define DPASS(f) DSYM("_" #f, f)

struct dsbuf {
    unsigned char *base;
    int size;
};

struct dfile {
    unsigned char *p;
    int r;
    int w;
    short flags;
    short file;
    struct dsbuf bf;
    int lbfsize;
    FILE *real;
    void *close;
    void *read;
    void *seek;
    void *write;
    struct dsbuf ub;
    void *extra;
    int ur;
    unsigned char ubuf[3];
    unsigned char nbuf[1];
    struct dsbuf lb;
    int blksize;
    int64_t offset;
};

struct dprogvars {
    const void *mh;
    int *argcp;
    char ***argvp;
    char ***envp;
    const char **prognamep;
};

extern int d_argc;
extern char **d_argv;
extern const char *d_progname;
extern char d_exec_path[];
extern const void *d_mh;

void *dsym_lookup(const char *name);
int d_errno_lx2d(int e);
int d_errno_d2lx(int e);
int *d___error(void);
void d_stdio_init(void);
void d_ctype_init(void);
void d_misc_init(void);
void d_errno_init(void);
void d_thread_main_init(void);
FILE *d_real_file(void *f);
int d_sig_d2lx(int s);
int d_sig_lx2d(int s);
int d_wstatus(int st);
int d_open_flags(int f);
int d_term_ioctl(int fd, unsigned long req, void *arg, int *handled);

#endif
