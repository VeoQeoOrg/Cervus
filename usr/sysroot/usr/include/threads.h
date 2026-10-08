#ifndef _THREADS_H
#define _THREADS_H

#include <pthread.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef pthread_t       thrd_t;
typedef pthread_mutex_t mtx_t;
typedef pthread_cond_t  cnd_t;
typedef pthread_key_t   tss_t;
typedef pthread_once_t  once_flag;

typedef int (*thrd_start_t)(void *);
typedef void (*tss_dtor_t)(void *);

enum {
	thrd_success  = 0,
	thrd_nomem    = 1,
	thrd_timedout = 2,
	thrd_busy     = 3,
	thrd_error    = 4,
};

enum {
	mtx_plain     = 0,
	mtx_recursive = 1,
	mtx_timed     = 2,
};

#define ONCE_FLAG_INIT PTHREAD_ONCE_INIT
#define TSS_DTOR_ITERATIONS 4

#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
#define thread_local _Thread_local
#endif

int    thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
thrd_t thrd_current(void);
int    thrd_detach(thrd_t thr);
int    thrd_equal(thrd_t a, thrd_t b);
void   thrd_exit(int res) __attribute__((noreturn));
int    thrd_join(thrd_t thr, int *res);
int    thrd_sleep(const struct timespec *duration, struct timespec *remaining);
void   thrd_yield(void);

int  mtx_init(mtx_t *mtx, int type);
void mtx_destroy(mtx_t *mtx);
int  mtx_lock(mtx_t *mtx);
int  mtx_timedlock(mtx_t *__restrict mtx, const struct timespec *__restrict ts);
int  mtx_trylock(mtx_t *mtx);
int  mtx_unlock(mtx_t *mtx);

void call_once(once_flag *flag, void (*func)(void));

int  cnd_init(cnd_t *cond);
void cnd_destroy(cnd_t *cond);
int  cnd_signal(cnd_t *cond);
int  cnd_broadcast(cnd_t *cond);
int  cnd_wait(cnd_t *cond, mtx_t *mtx);
int  cnd_timedwait(cnd_t *__restrict cond, mtx_t *__restrict mtx, const struct timespec *__restrict ts);

int   tss_create(tss_t *key, tss_dtor_t dtor);
void  tss_delete(tss_t key);
int   tss_set(tss_t key, void *val);
void *tss_get(tss_t key);

#ifdef __cplusplus
}
#endif

#endif
