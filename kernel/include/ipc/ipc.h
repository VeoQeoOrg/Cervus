#ifndef CERVUS_IPC_H
#define CERVUS_IPC_H

#include <stdint.h>
#include <stdbool.h>
#include "../sched/spinlock.h"

struct task;

#define IPC_SLOTS      256

#define IPC_PRIVATE_K  0
#define IPC_CREAT_K    01000
#define IPC_EXCL_K     02000
#define IPC_NOWAIT_K   04000

#define IPC_RMID_K     0
#define IPC_SET_K      1
#define IPC_STAT_K     2

#define IPC_R          0444
#define IPC_W          0222

typedef struct ipc_obj {
    int32_t  key;
    int32_t  id;
    uint32_t uid, gid, cuid, cgid;
    uint32_t mode;
    uint16_t seq;
    int      dead;
    int      refs;
} ipc_obj_t;

typedef struct {
    int32_t  key;
    uint32_t uid, gid, cuid, cgid;
    uint16_t mode, pad1, seq, pad2;
    uint64_t unused1, unused2;
} ipc_perm_user_t;

typedef struct ipc_ns {
    spinlock_t lock;
    ipc_obj_t *slot[IPC_SLOTS];
    uint16_t   seq;
    void     (*destroy)(ipc_obj_t *o);
} ipc_ns_t;

typedef ipc_obj_t *(*ipc_create_fn)(void *arg, int *err);
typedef int (*ipc_check_fn)(ipc_obj_t *o, void *arg);

int        ipc_get(ipc_ns_t *ns, int32_t key, int flags, ipc_create_fn create, ipc_check_fn check, void *arg);
ipc_obj_t *ipc_lookup_locked(ipc_ns_t *ns, int id);
bool       ipc_perm_ok(ipc_obj_t *o, struct task *t, int want);
bool       ipc_owner_ok(ipc_obj_t *o, struct task *t);
void       ipc_fill_perm(ipc_obj_t *o, ipc_perm_user_t *p);
void       ipc_set_perm(ipc_obj_t *o, const ipc_perm_user_t *p);
void       ipc_remove_locked(ipc_ns_t *ns, ipc_obj_t *o);
void       ipc_put_locked(ipc_ns_t *ns, ipc_obj_t *o);
int64_t    ipc_now(void);

#endif
