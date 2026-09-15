#include <pthread.h>
#include <errno.h>
#include <string.h>

typedef struct {
    int   used;
    void (*destructor)(void *);
} key_slot_t;

static key_slot_t g_keys[PTHREAD_KEYS_MAX];
static pthread_mutex_t g_key_lock = PTHREAD_MUTEX_INITIALIZER;

static __thread void *g_values[PTHREAD_KEYS_MAX];

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
    return g_values[key];
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    g_values[key] = (void *)value;
    return 0;
}

void __cervus_pthread_key_cleanup(void)
{
    for (int round = 0; round < 4; round++) {
        int any = 0;
        for (unsigned i = 0; i < PTHREAD_KEYS_MAX; i++) {
            void *v = g_values[i];
            if (!v || !g_keys[i].used || !g_keys[i].destructor) continue;
            g_values[i] = 0;
            g_keys[i].destructor(v);
            any = 1;
        }
        if (!any) break;
    }
}
