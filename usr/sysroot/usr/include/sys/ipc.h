#ifndef _SYS_IPC_H
#define _SYS_IPC_H
#ifdef __cplusplus
extern "C" {
#endif

typedef int key_t;

#define IPC_PRIVATE  0
#define IPC_CREAT    01000
#define IPC_EXCL     02000

#define IPC_RMID     0
#define IPC_STAT     2

#ifdef __cplusplus
}
#endif
#endif
