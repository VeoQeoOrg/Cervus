#ifndef _SYS_MSG_H
#define _SYS_MSG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <time.h>
#include <sys/ipc.h>

typedef unsigned long msgqnum_t;
typedef unsigned long msglen_t;

struct msqid_ds {
    struct ipc_perm msg_perm;
    time_t          msg_stime;
    time_t          msg_rtime;
    time_t          msg_ctime;
    unsigned long   __msg_cbytes;
    msgqnum_t       msg_qnum;
    msglen_t        msg_qbytes;
    pid_t           msg_lspid;
    pid_t           msg_lrpid;
    unsigned long   __unused[2];
};

#define MSG_NOERROR 010000
#define MSG_EXCEPT  020000

int     msgget(key_t key, int msgflg);
int     msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);
ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);
int     msgctl(int msqid, int cmd, struct msqid_ds *buf);

#ifdef __cplusplus
}
#endif
#endif
