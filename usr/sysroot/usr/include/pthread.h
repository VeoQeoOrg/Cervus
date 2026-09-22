#ifndef _PTHREAD_H
#define _PTHREAD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef struct __pthread *pthread_t;

typedef struct {
    size_t stacksize;
    int    detachstate;
} pthread_attr_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

typedef struct { volatile int state; int type; } pthread_mutex_t;
typedef struct { int type; } pthread_mutexattr_t;
#define PTHREAD_MUTEX_INITIALIZER { 0, 0 }

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
pthread_t pthread_self(void);
int  pthread_equal(pthread_t a, pthread_t b);

int  pthread_attr_init(pthread_attr_t *a);
int  pthread_attr_destroy(pthread_attr_t *a);
int  pthread_attr_setstacksize(pthread_attr_t *a, size_t sz);
int  pthread_attr_getstacksize(const pthread_attr_t *a, size_t *sz);
int  pthread_attr_setdetachstate(pthread_attr_t *a, int state);

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

#ifdef __cplusplus
}
#endif
#endif
