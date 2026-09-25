#ifndef _LIBCERVUS_PRIV_H
#define _LIBCERVUS_PRIV_H

#include <stddef.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/types.h>

#define CERVUS_PATH_MAX 512

extern int    __cervus_errno;
extern int    __cervus_argc;
extern char **__cervus_argv;

long __cervus_sys_ret(long r);

long long __cervus_parse_signed(const char *s, char **end, int base, int is_unsigned);

typedef struct { volatile int state; } __cervus_lock_t;

#define CERVUS_LOCK_INIT { 0 }

void  *__cervus_tls_alloc(void);
int    __cervus_tls_set(void *tp);
void   __cervus_tls_free(void *tp);
void   __cervus_tls_init(void);

#define __CERVUS_TCB_SELF 2
#define __CERVUS_TCB_KEYS 3

static inline void **__cervus_tcb(void) {
    void **tp;
    __asm__ ("mov %%fs:0, %0" : "=r"(tp));
    return tp;
}

void __cervus_pthread_key_cleanup(void);

void __cervus_lock(__cervus_lock_t *l);
void __cervus_unlock(__cervus_lock_t *l);
extern __cervus_lock_t __cervus_heap_lock;

#define __CF_OWNED   1

#define __CBUF_UNSET 0
#define __CBUF_FULL  1
#define __CBUF_LINE  2
#define __CBUF_NONE  3

#define __CDIR_IDLE  0
#define __CDIR_READ  1
#define __CDIR_WRITE 2

typedef struct {
    ssize_t (*read)(void *cookie, char *buf, size_t n);
    ssize_t (*write)(void *cookie, const char *buf, size_t n);
    int     (*seek)(void *cookie, off_t *pos, int whence);
    int     (*close)(void *cookie);
} __cervus_io_funcs_t;

struct __cervus_FILE {
    int    fd;
    int    eof;
    int    err;
    int    flags;
    char  *buf;
    size_t buf_size;
    size_t buf_pos;
    int    unget;
    int    bufmode;
    int    dir;
    size_t buf_len;
    __cervus_lock_t lock;
    int    has_io;
    void  *cookie;
    __cervus_io_funcs_t io;
};

ssize_t __cervus_io_read(struct __cervus_FILE *s, void *buf, size_t n);
ssize_t __cervus_io_write(struct __cervus_FILE *s, const void *buf, size_t n);
off_t   __cervus_io_seek(struct __cervus_FILE *s, off_t off, int whence);
int     __cervus_io_close(struct __cervus_FILE *s);
size_t  __cervus_io_write_all(struct __cervus_FILE *s, const char *p, size_t total);

int  __cervus_fflush(struct __cervus_FILE *s);
void __cervus_stream_register(struct __cervus_FILE *s);
void __cervus_stream_forget(struct __cervus_FILE *s);
void __cervus_flush_all(void);
#define __CERVUS_STDIO_BUFSZ 16384
int  __cervus_fill(struct __cervus_FILE *s);
void __cervus_setup_buf(struct __cervus_FILE *s);

struct __cervus_DIR {
    int fd;
    struct dirent buf;
};

typedef struct __mblock {
    size_t size;
    size_t prev_size;
} __mblock_t;

#define MB_HDR_SZ        (sizeof(__mblock_t))
#define MB_ALIGN         16
#define MB_MIN_TOTAL     32
#define MB_FREE_BIT      ((size_t)1)
#define MB_SIZE(b)       ((b)->size & ~MB_FREE_BIT)
#define MB_IS_FREE(b)    (((b)->size & MB_FREE_BIT) != 0)
#define MB_USER(b)       ((void *)((char *)(b) + MB_HDR_SZ))
#define MB_FROM_USER(p)  ((__mblock_t *)((char *)(p) - MB_HDR_SZ))

extern __mblock_t *__cervus_heap_start;
extern __mblock_t *__cervus_heap_end;

__mblock_t *__cervus_heap_grow(size_t need);
void        __cervus_mb_split(__mblock_t *b, size_t need);

static inline size_t __cervus_align_up(size_t n, size_t a) {
    return (n + a - 1) & ~(a - 1);
}

static inline __mblock_t *__cervus_mb_next(__mblock_t *b) {
    return (__mblock_t *)((char *)b + MB_SIZE(b));
}

static inline __mblock_t *__cervus_mb_prev(__mblock_t *b) {
    if (b->prev_size == 0) return (__mblock_t *)0;
    return (__mblock_t *)((char *)b - b->prev_size);
}

int  __cervus_exit_push(void (*fn)(void *), void *arg, void *dso, int plain);
void __cervus_run_exit_fns(void *dso);

typedef struct {
    void *(*open)(const char *path, int flags);
    void *(*sym)(void *handle, const char *name);
    int   (*close)(void *handle);
    void  (*init)(int argc, char **argv, char **envp);
} __cervus_dl_ops_t;

extern __cervus_dl_ops_t *__cervus_dl_ops;
typedef struct {
    void (**preinit_start)(int, char **, char **);
    void (**preinit_end)(int, char **, char **);
    void (**init_start)(int, char **, char **);
    void (**init_end)(int, char **, char **);
    void (**fini_start)(void);
    void (**fini_end)(void);
} __cervus_image_t;

void __cervus_run_init(int argc, char **argv, char **envp, const __cervus_image_t *img);

int __cervus_is_leap(int y);
extern const int __cervus_days_in_mon[2][12];

#endif
