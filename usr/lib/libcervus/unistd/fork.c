#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

void __cervus_atfork_prepare(void);
void __cervus_atfork_parent(void);
void __cervus_atfork_child(void);

__attribute__((weak)) void __cervus_thread_forked(void);
__attribute__((weak)) void __cervus_dl_atfork(int phase);
__attribute__((weak)) void __cervus_stdio_atfork(int phase);
__attribute__((weak)) void __cervus_tz_atfork(int phase);
__attribute__((weak)) void __cervus_intl_atfork(int phase);
__attribute__((weak)) void __cervus_key_atfork(int phase);
__attribute__((weak)) void __cervus_sem_atfork(int phase);
__attribute__((weak)) void __cervus_quick_exit_atfork(int phase);
__attribute__((weak)) void __cervus_tls_atfork(int phase);
__attribute__((weak)) void __cervus_pthread_atfork(int phase);
__attribute__((weak)) void __cervus_heap_atfork(int phase);

static void (*const g_fork_locks[])(int) = {
    __cervus_dl_atfork,
    __cervus_stdio_atfork,
    __cervus_tz_atfork,
    __cervus_intl_atfork,
    __cervus_key_atfork,
    __cervus_sem_atfork,
    __cervus_quick_exit_atfork,
    __cervus_tls_atfork,
    __cervus_pthread_atfork,
    __cervus_heap_atfork,
};

#define N_FORK_LOCKS ((int)(sizeof g_fork_locks / sizeof g_fork_locks[0]))

pid_t fork(void)
{
    __cervus_atfork_prepare();
    for (int i = 0; i < N_FORK_LOCKS; i++)
        if (g_fork_locks[i]) g_fork_locks[i](-1);
    pid_t r = (pid_t)__cervus_sys_ret(syscall0(SYS_FORK));
    if (r == 0 && __cervus_thread_forked) __cervus_thread_forked();
    for (int i = N_FORK_LOCKS - 1; i >= 0; i--)
        if (g_fork_locks[i]) g_fork_locks[i](r == 0);
    if (r == 0) __cervus_atfork_child();
    else __cervus_atfork_parent();
    return r;
}
