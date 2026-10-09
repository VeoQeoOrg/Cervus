#include <sys/sem.h>
#include <sys/syscall.h>
#include <libcervus.h>
#include <stdarg.h>

union semun_arg {
    int              val;
    struct semid_ds *buf;
    unsigned short  *array;
};

int semget(key_t key, int nsems, int semflg) {
    return (int)__cervus_sys_ret(syscall3(SYS_SEMGET, key, nsems, semflg));
}

int semop(int semid, struct sembuf *sops, size_t nsops) {
    int cp = __cervus_cp_enter();
    long r = syscall3(SYS_SEMOP, semid, sops, nsops);
    __cervus_cp_leave(cp);
    return (int)__cervus_sys_ret(r);
}

int semtimedop(int semid, struct sembuf *sops, size_t nsops, const struct timespec *timeout) {
    int cp = __cervus_cp_enter();
    long r = syscall4(SYS_SEMTIMEDOP, semid, sops, nsops, timeout);
    __cervus_cp_leave(cp);
    return (int)__cervus_sys_ret(r);
}

int semctl(int semid, int semnum, int cmd, ...) {
    union semun_arg arg = { 0 };
    long raw = 0;
    switch (cmd) {
    case SETVAL:
    case GETALL:
    case SETALL:
    case IPC_SET:
    case IPC_STAT: {
        va_list ap;
        va_start(ap, cmd);
        arg = va_arg(ap, union semun_arg);
        va_end(ap);
        raw = cmd == SETVAL ? (long)arg.val : (long)arg.buf;
        break;
    }
    }
    return (int)__cervus_sys_ret(syscall4(SYS_SEMCTL, semid, semnum, cmd, raw));
}
