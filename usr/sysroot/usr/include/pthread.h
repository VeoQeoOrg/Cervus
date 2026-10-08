#ifndef _PTHREAD_H
#define _PTHREAD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <sched.h>
#include <time.h>

typedef struct __pthread *pthread_t;

typedef struct {
    size_t stacksize;
    int    detachstate;
} pthread_attr_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

typedef struct { volatile int state; int type; void *volatile owner; int count; } pthread_mutex_t;
typedef struct { int type; int pshared; } pthread_mutexattr_t;
#define PTHREAD_MUTEX_INITIALIZER { 0, 0, 0, 0 }
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP { 0, 1, 0, 0 }

#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_NORMAL

#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED  1

typedef struct { volatile int seq; } pthread_cond_t;
typedef struct { int unused; } pthread_condattr_t;
#define PTHREAD_COND_INITIALIZER { 0 }

typedef struct { volatile int state; } pthread_once_t;

typedef unsigned int pthread_key_t;

#define PTHREAD_KEYS_MAX 64
#define PTHREAD_ONCE_INIT { 0 }

typedef struct {
    volatile int readers;
    pthread_mutex_t w;
} pthread_rwlock_t;
#define PTHREAD_RWLOCK_INITIALIZER { 0, PTHREAD_MUTEX_INITIALIZER }

int  pthread_create(pthread_t *th, const pthread_attr_t *attr,
                    void *(*fn)(void *), void *arg);
int  pthread_join(pthread_t th, void **retval);
int  pthread_detach(pthread_t th);
void pthread_exit(void *retval) __attribute__((noreturn));

#define PTHREAD_CANCEL_ENABLE       0
#define PTHREAD_CANCEL_DISABLE      1
#define PTHREAD_CANCEL_DEFERRED     0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#define PTHREAD_CANCELED            ((void *)-1)

int  pthread_cancel(pthread_t th);
int  pthread_setcancelstate(int state, int *oldstate);
int  pthread_setcanceltype(int type, int *oldtype);
void pthread_testcancel(void);
int  pthread_kill(pthread_t th, int sig);

struct __pthread_cleanup {
    void (*fn)(void *);
    void *arg;
    struct __pthread_cleanup *next;
};
void __pthread_cleanup_push(struct __pthread_cleanup *c, void (*fn)(void *), void *arg);
void __pthread_cleanup_pop(struct __pthread_cleanup *c, int execute);
#define pthread_cleanup_push(fn, arg) do { struct __pthread_cleanup __pcb; __pthread_cleanup_push(&__pcb, (fn), (arg));
#define pthread_cleanup_pop(execute) __pthread_cleanup_pop(&__pcb, (execute)); } while (0)
pthread_t pthread_self(void);
int  pthread_equal(pthread_t a, pthread_t b);

int  pthread_attr_init(pthread_attr_t *a);
int  pthread_attr_destroy(pthread_attr_t *a);
int  pthread_attr_setstacksize(pthread_attr_t *a, size_t sz);
int  pthread_attr_getstacksize(const pthread_attr_t *a, size_t *sz);
int  pthread_attr_setdetachstate(pthread_attr_t *a, int state);

int  pthread_mutexattr_init(pthread_mutexattr_t *a);
int  pthread_mutexattr_destroy(pthread_mutexattr_t *a);
int  pthread_mutexattr_settype(pthread_mutexattr_t *a, int type);
int  pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *type);
int  pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int pshared);
int  pthread_mutexattr_getpshared(const pthread_mutexattr_t *a, int *pshared);
int  pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a);
int  pthread_mutex_destroy(pthread_mutex_t *m);
int  pthread_mutex_lock(pthread_mutex_t *m);
int  pthread_mutex_trylock(pthread_mutex_t *m);
int  pthread_mutex_unlock(pthread_mutex_t *m);

int  pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a);
int  pthread_cond_destroy(pthread_cond_t *c);
int  pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int  pthread_cond_signal(pthread_cond_t *c);
int  pthread_cond_broadcast(pthread_cond_t *c);

int  pthread_once(pthread_once_t *o, void (*fn)(void));

int   pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int   pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int   pthread_setspecific(pthread_key_t key, const void *value);

int  pthread_rwlock_init(pthread_rwlock_t *l, void *a);
int  pthread_rwlock_destroy(pthread_rwlock_t *l);
int  pthread_rwlock_rdlock(pthread_rwlock_t *l);
int  pthread_rwlock_wrlock(pthread_rwlock_t *l);
int  pthread_rwlock_unlock(pthread_rwlock_t *l);
int  pthread_rwlock_tryrdlock(pthread_rwlock_t *l);
int  pthread_rwlock_trywrlock(pthread_rwlock_t *l);

int  pthread_condattr_init(pthread_condattr_t *a);
int  pthread_condattr_destroy(pthread_condattr_t *a);
int  pthread_condattr_setclock(pthread_condattr_t *a, int clock);
int  pthread_condattr_getclock(const pthread_condattr_t *a, int *clock);
int  pthread_condattr_setpshared(pthread_condattr_t *a, int pshared);
int  pthread_condattr_getpshared(const pthread_condattr_t *a, int *pshared);
struct timespec;
int  pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime);
int  pthread_cond_clockwait(pthread_cond_t *c, pthread_mutex_t *m, int clock, const struct timespec *abstime);

struct sched_param;
int  pthread_getschedparam(pthread_t th, int *policy, struct sched_param *param);
int  pthread_setschedparam(pthread_t th, int policy, const struct sched_param *param);
int  pthread_setschedprio(pthread_t th, int prio);
int  pthread_attr_getdetachstate(const pthread_attr_t *a, int *state);
int  pthread_setname_np(pthread_t th, const char *name);
int  pthread_getname_np(pthread_t th, char *name, size_t len);
int  pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));

int  pthread_mutex_timedlock(pthread_mutex_t *__restrict m, const struct timespec *__restrict abstime);
int  pthread_mutex_clocklock(pthread_mutex_t *__restrict m, int clock, const struct timespec *__restrict abstime);
int  pthread_rwlock_timedrdlock(pthread_rwlock_t *__restrict l, const struct timespec *__restrict abstime);
int  pthread_rwlock_timedwrlock(pthread_rwlock_t *__restrict l, const struct timespec *__restrict abstime);

typedef struct { int pshared; } pthread_rwlockattr_t;
int  pthread_rwlockattr_init(pthread_rwlockattr_t *a);
int  pthread_rwlockattr_destroy(pthread_rwlockattr_t *a);
int  pthread_rwlockattr_setpshared(pthread_rwlockattr_t *a, int pshared);
int  pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *__restrict a, int *__restrict pshared);

typedef volatile int pthread_spinlock_t;
int  pthread_spin_init(pthread_spinlock_t *l, int pshared);
int  pthread_spin_destroy(pthread_spinlock_t *l);
int  pthread_spin_lock(pthread_spinlock_t *l);
int  pthread_spin_trylock(pthread_spinlock_t *l);
int  pthread_spin_unlock(pthread_spinlock_t *l);

#define PTHREAD_BARRIER_SERIAL_THREAD (-1)
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  c;
    unsigned        count;
    unsigned        waiting;
    unsigned        cycle;
} pthread_barrier_t;
typedef struct { int pshared; } pthread_barrierattr_t;
int  pthread_barrierattr_init(pthread_barrierattr_t *a);
int  pthread_barrierattr_destroy(pthread_barrierattr_t *a);
int  pthread_barrierattr_setpshared(pthread_barrierattr_t *a, int pshared);
int  pthread_barrierattr_getpshared(const pthread_barrierattr_t *__restrict a, int *__restrict pshared);
int  pthread_barrier_init(pthread_barrier_t *__restrict b, const pthread_barrierattr_t *__restrict a, unsigned count);
int  pthread_barrier_destroy(pthread_barrier_t *b);
int  pthread_barrier_wait(pthread_barrier_t *b);

int  pthread_getconcurrency(void);
int  pthread_setconcurrency(int level);

#ifdef __cplusplus
}
#endif
#endif
