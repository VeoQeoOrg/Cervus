#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <libcervus.h>

typedef struct {
    int   used;
    void (*destructor)(void *);
} key_slot_t;

static key_slot_t g_keys[PTHREAD_KEYS_MAX];
static pthread_mutex_t g_key_lock = PTHREAD_MUTEX_INITIALIZER;

static void **key_values(int create)
{
    void **tcb = __cervus_tcb();
    void **vals = tcb[__CERVUS_TCB_KEYS];
    if (!vals && create) {
        vals = calloc(PTHREAD_KEYS_MAX, sizeof(void *));
        tcb[__CERVUS_TCB_KEYS] = vals;
    }
    return vals;
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    if (!key) return EINVAL;

    pthread_mutex_lock(&g_key_lock);
    for (unsigned i = 0; i < PTHREAD_KEYS_MAX; i++) {
        if (g_keys[i].used) continue;
        g_keys[i].used = 1;
        g_keys[i].destructor = destructor;
        pthread_mutex_unlock(&g_key_lock);
        *key = i;
        return 0;
    }
    pthread_mutex_unlock(&g_key_lock);
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key)
{
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    pthread_mutex_lock(&g_key_lock);
    g_keys[key].used = 0;
    g_keys[key].destructor = 0;
    pthread_mutex_unlock(&g_key_lock);
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    if (key >= PTHREAD_KEYS_MAX) return 0;
    void **vals = key_values(0);
    return vals ? vals[key] : 0;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    void **vals = key_values(1);
    if (!vals) return ENOMEM;
    vals[key] = (void *)value;
    return 0;
}

void __cervus_pthread_key_cleanup(void)
{
    void **vals = key_values(0);
    if (!vals) return;
    for (int round = 0; round < 4; round++) {
        int any = 0;
        for (unsigned i = 0; i < PTHREAD_KEYS_MAX; i++) {
            void *v = vals[i];
            if (!v || !g_keys[i].used || !g_keys[i].destructor) continue;
            vals[i] = 0;
            g_keys[i].destructor(v);
            any = 1;
        }
        if (!any) break;
    }
    __cervus_tcb()[__CERVUS_TCB_KEYS] = 0;
    free(vals);
}
