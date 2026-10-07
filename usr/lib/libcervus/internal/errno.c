#include <libcervus.h>

int __cervus_errno = 0;
int h_errno = 0;
int __cervus_tls_live = 0;

int *__errno_location(void)
{
    if (__atomic_load_n(&__cervus_tls_live, __ATOMIC_ACQUIRE))
        return (int *)__cervus_tcb()[__CERVUS_TCB_ERRNO];
    return &__cervus_errno;
}

int *__h_errno_location(void)
{
    if (__atomic_load_n(&__cervus_tls_live, __ATOMIC_ACQUIRE))
        return (int *)__cervus_tcb()[__CERVUS_TCB_HERRNO];
    return &h_errno;
}

void __cervus_set_errno(int e)
{
    *__errno_location() = e;
    __cervus_errno = e;
}

void __cervus_set_h_errno(int e)
{
    *__h_errno_location() = e;
    h_errno = e;
}
