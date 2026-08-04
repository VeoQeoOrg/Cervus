#ifndef _SYS_SHM_H
#define _SYS_SHM_H

#include <stddef.h>
#include <sys/ipc.h>

#define SHM_RDONLY 010000

int   shmget(key_t key, size_t size, int shmflg);
void *shmat(int shmid, const void *shmaddr, int shmflg);
int   shmdt(const void *shmaddr);
int   shmctl(int shmid, int cmd, void *buf);

#endif
