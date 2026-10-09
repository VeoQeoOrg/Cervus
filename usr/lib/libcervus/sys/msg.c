#include <sys/msg.h>
#include <sys/syscall.h>
#include <libcervus.h>

int msgget(key_t key, int msgflg) {
    return (int)__cervus_sys_ret(syscall2(SYS_MSGGET, key, msgflg));
}

int msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg) {
    int cp = __cervus_cp_enter();
    long r = syscall4(SYS_MSGSND, msqid, msgp, msgsz, msgflg);
    __cervus_cp_leave(cp);
    return (int)__cervus_sys_ret(r);
}

ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg) {
    int cp = __cervus_cp_enter();
    long r = syscall5(SYS_MSGRCV, msqid, msgp, msgsz, msgtyp, msgflg);
    __cervus_cp_leave(cp);
    return (ssize_t)__cervus_sys_ret(r);
}

int msgctl(int msqid, int cmd, struct msqid_ds *buf) {
    return (int)__cervus_sys_ret(syscall3(SYS_MSGCTL, msqid, cmd, buf));
}
