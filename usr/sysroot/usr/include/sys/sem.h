#ifndef _SYS_SEM_H
#define _SYS_SEM_H

#include <stddef.h>
#include <sys/ipc.h>

struct sembuf {
    unsigned short sem_num;
    short          sem_op;
    short          sem_flg;
};

#define SEM_UNDO   0x1000
#define IPC_NOWAIT 04000
#define GETVAL     12
#define SETVAL     16

int semget(key_t key, int nsems, int semflg);
int semop(int semid, struct sembuf *sops, size_t nsops);
int semctl(int semid, int semnum, int cmd, ...);

#endif
