#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

typedef struct {
    volatile int __value;
    volatile int __waiters;
    int __pshared;
    int __pad;
} sem_t;

#define SEM_FAILED ((sem_t *)0)
#define SEM_VALUE_MAX 0x7FFFFFFF

int    sem_init(sem_t *sem, int pshared, unsigned value);
int    sem_destroy(sem_t *sem);
int    sem_post(sem_t *sem);
int    sem_wait(sem_t *sem);
int    sem_trywait(sem_t *sem);
int    sem_timedwait(sem_t *sem, const struct timespec *abstime);
int    sem_clockwait(sem_t *sem, clockid_t clock, const struct timespec *abstime);
int    sem_getvalue(sem_t *sem, int *value);
sem_t *sem_open(const char *name, int oflag, ...);
int    sem_close(sem_t *sem);
int    sem_unlink(const char *name);

#ifdef __cplusplus
}
#endif
#endif
