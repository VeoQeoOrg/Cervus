#ifndef _SYS_SEM_H
#define _SYS_SEM_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <time.h>
#include <sys/ipc.h>

struct sembuf {
    unsigned short sem_num;
    short          sem_op;
    short          sem_flg;
};

struct semid_ds {
    struct ipc_perm sem_perm;
    time_t          sem_otime;
    unsigned long   __unused1;
    time_t          sem_ctime;
    unsigned long   __unused2;
    unsigned short  sem_nsems;
    char            __sem_nsems_pad[sizeof(long) - sizeof(short)];
    unsigned long   __unused3;
    unsigned long   __unused4;
};

#define SEM_UNDO 0x1000

#define GETPID   11
#define GETVAL   12
#define GETALL   13
#define GETNCNT  14
#define GETZCNT  15
#define SETVAL   16
#define SETALL   17

int semget(key_t key, int nsems, int semflg);
int semop(int semid, struct sembuf *sops, size_t nsops);
int semtimedop(int semid, struct sembuf *sops, size_t nsops, const struct timespec *timeout);
int semctl(int semid, int semnum, int cmd, ...);

#ifdef __cplusplus
}
#endif
#endif
