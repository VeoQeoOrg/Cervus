#include <sys/shm.h>
#include <sys/syscall.h>
#include <libcervus.h>

int shmget(key_t key, size_t size, int shmflg) {
    return (int)__cervus_sys_ret(syscall3(SYS_SHMGET, key, size, shmflg));
}

void *shmat(int shmid, const void *shmaddr, int shmflg) {
    long r = __cervus_sys_ret(syscall3(SYS_SHMAT, shmid, shmaddr, shmflg));
    return r < 0 ? (void *)-1 : (void *)r;
}

int shmdt(const void *shmaddr) {
    return (int)__cervus_sys_ret(syscall1(SYS_SHMDT, shmaddr));
}

int shmctl(int shmid, int cmd, void *buf) {
    return (int)__cervus_sys_ret(syscall3(SYS_SHMCTL, shmid, cmd, buf));
}
