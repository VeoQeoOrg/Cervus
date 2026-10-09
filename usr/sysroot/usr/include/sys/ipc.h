#ifndef _SYS_IPC_H
#define _SYS_IPC_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#ifndef __CERVUS_KEY_T
#define __CERVUS_KEY_T
typedef int key_t;
#endif

struct ipc_perm {
    key_t          __key;
    uid_t          uid;
    gid_t          gid;
    uid_t          cuid;
    gid_t          cgid;
    unsigned short mode;
    unsigned short __pad1;
    unsigned short __seq;
    unsigned short __pad2;
    unsigned long  __unused1;
    unsigned long  __unused2;
};

key_t ftok(const char *path, int id);

#define IPC_PRIVATE  ((key_t)0)
#define IPC_CREAT    01000
#define IPC_EXCL     02000
#define IPC_NOWAIT   04000

#define IPC_RMID     0
#define IPC_SET      1
#define IPC_STAT     2
#define IPC_INFO     3

#ifdef __cplusplus
}
#endif
#endif
