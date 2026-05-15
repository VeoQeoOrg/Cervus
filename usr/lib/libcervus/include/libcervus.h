#ifndef _LIBCERVUS_PRIV_H
#define _LIBCERVUS_PRIV_H

#include <stddef.h>
#include <stdint.h>
#include <dirent.h>

#define CERVUS_PATH_MAX 512

extern int    __cervus_errno;
extern int    __cervus_argc;
extern char **__cervus_argv;

extern char __cervus_cwd[CERVUS_PATH_MAX];
extern int  __cervus_cwd_inited;

long        __cervus_sys_ret(long r);
const char *__cervus_get_cwd(void);
const char *__cervus_resolve(const char *path, char *buf, size_t bufsz);

#define _CERVUS_FILT_MAX 128
extern char *__cervus_filtered_argv[_CERVUS_FILT_MAX + 1];
int  __cervus_filter_args(int argc, char **argv);

long long __cervus_parse_signed(const char *s, char **end, int base, int is_unsigned);

struct __cervus_FILE {
    int    fd;
    int    eof;
    int    err;
    int    flags;
    char  *buf;
    size_t buf_size;
    size_t buf_pos;
};

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

extern char **__cervus_env_table;
extern int    __cervus_env_count;
extern int    __cervus_env_cap;

extern void (*__cervus_atexit_fns[])(void);
extern int   __cervus_atexit_cnt;
extern int   __cervus_atexit_max;

int __cervus_is_leap(int y);
extern const int __cervus_days_in_mon[2][12];

#endif
