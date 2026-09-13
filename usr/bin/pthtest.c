#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: pthtest [threads] [iterations]\n"
    "Exercise threads, a mutex, a condition variable and the allocator.\n";

#define MAXTH 32

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cond = PTHREAD_COND_INITIALIZER;
static long            g_counter;
static int             g_ready;
static int             g_iters = 20000;

static void *counter_thread(void *arg)
{
    long id = (long)arg;
    for (int i = 0; i < g_iters; i++) {
        pthread_mutex_lock(&g_lock);
        g_counter++;
        pthread_mutex_unlock(&g_lock);
    }
    return (void *)(id * 2);
}

static void *malloc_thread(void *arg)
{
    (void)arg;
    for (int i = 0; i < 2000; i++) {
        size_t n = 16 + (size_t)(i % 400);
        char *p = malloc(n);
        if (!p) return (void *)-1;
        memset(p, (int)(i & 0xFF), n);
        for (size_t k = 0; k < n; k++)
            if ((unsigned char)p[k] != (unsigned char)(i & 0xFF)) {
                free(p);
                return (void *)-1;
            }
        free(p);
    }
    return NULL;
}

static void *waiter_thread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_lock);
    while (!g_ready) pthread_cond_wait(&g_cond, &g_lock);
    pthread_mutex_unlock(&g_lock);
    return (void *)1;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "pthtest")) return 0;
    argc = cervus_end_of_options(argc, argv);

    int nth = (argc > 1) ? atoi(argv[1]) : 4;
    if (nth < 1) nth = 1;
    if (nth > MAXTH) nth = MAXTH;
    if (argc > 2) g_iters = atoi(argv[2]);
    if (g_iters < 1) g_iters = 1;

    pthread_t th[MAXTH];
    int fails = 0;

    printf("counter: %d threads x %d increments\n", nth, g_iters);
    for (int i = 0; i < nth; i++)
        if (pthread_create(&th[i], NULL, counter_thread, (void *)(long)i) != 0) {
            printf("  pthread_create failed at %d\n", i);
            return 1;
        }
    for (int i = 0; i < nth; i++) {
        void *r = NULL;
        pthread_join(th[i], &r);
        if ((long)r != (long)i * 2) { printf("  bad return from %d\n", i); fails++; }
    }
    long want = (long)nth * g_iters;
    printf("  counter = %ld, expected %ld : %s\n",
           g_counter, want, g_counter == want ? "OK" : "MISMATCH");
    if (g_counter != want) fails++;

    printf("allocator: %d threads hammering malloc\n", nth);
    for (int i = 0; i < nth; i++)
        pthread_create(&th[i], NULL, malloc_thread, NULL);
    for (int i = 0; i < nth; i++) {
        void *r = NULL;
        pthread_join(th[i], &r);
        if (r) { printf("  corruption in thread %d\n", i); fails++; }
    }
    printf("  no corruption : %s\n", fails ? "FAILED" : "OK");

    printf("condition variable: %d waiters\n", nth);
    g_ready = 0;
    for (int i = 0; i < nth; i++)
        pthread_create(&th[i], NULL, waiter_thread, NULL);
    sleep(1);
    pthread_mutex_lock(&g_lock);
    g_ready = 1;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
    for (int i = 0; i < nth; i++) {
        void *r = NULL;
        pthread_join(th[i], &r);
        if ((long)r != 1) { printf("  waiter %d did not wake\n", i); fails++; }
    }
    printf("  all woke : %s\n", fails ? "FAILED" : "OK");

    printf(fails ? "pthtest: %d FAILURES\n" : "pthtest: all ok\n", fails);
    return fails != 0;
}
