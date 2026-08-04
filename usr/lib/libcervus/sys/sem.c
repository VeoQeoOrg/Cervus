#include <sys/sem.h>
#include <sys/syscall.h>
#include <libcervus.h>
#include <stdarg.h>

int semget(key_t key, int nsems, int semflg) {
    return (int)__cervus_sys_ret(syscall3(SYS_SEMGET, key, nsems, semflg));
}

int semop(int semid, struct sembuf *sops, size_t nsops) {
    return (int)__cervus_sys_ret(syscall3(SYS_SEMOP, semid, sops, nsops));
}

int semctl(int semid, int semnum, int cmd, ...) {
    long arg = 0;
    if (cmd == SETVAL) { va_list ap; va_start(ap, cmd); arg = va_arg(ap, int); va_end(ap); }
    return (int)__cervus_sys_ret(syscall4(SYS_SEMCTL, semid, semnum, cmd, arg));
}
