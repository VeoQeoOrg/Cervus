#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include <string.h>

#define SEM_MAX        32
#define SEM_NSEMS_MAX  32
#define IPC_CREAT      01000
#define IPC_RMID       0
#define IPC_NOWAIT     04000
#define GETVAL         12
#define SETVAL         16

extern void task_sleep_ms(uint64_t ms);

struct k_sembuf { unsigned short sem_num; short sem_op; short sem_flg; };

typedef struct { int used, key, id, nsems; int val[SEM_NSEMS_MAX]; int marked; } sem_set_t;

static sem_set_t g_sems[SEM_MAX];
static spinlock_t g_lock = SPINLOCK_INIT;
static int g_next_id = 1;

static sem_set_t *set_by_id(int id) {
    for (int i = 0; i < SEM_MAX; i++) if (g_sems[i].used && g_sems[i].id == id) return &g_sems[i];
    return 0;
}

int64_t sys_semget(uint64_t key, uint64_t nsems, uint64_t flags) {
    if (nsems > SEM_NSEMS_MAX) return -EINVAL;
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    if (key != 0) {
        for (int i = 0; i < SEM_MAX; i++) if (g_sems[i].used && g_sems[i].key == (int)key) { int id = g_sems[i].id; spinlock_release_irqrestore(&g_lock, f); return id; }
        if (!(flags & IPC_CREAT)) { spinlock_release_irqrestore(&g_lock, f); return -ENOENT; }
    }
    if (nsems == 0) { spinlock_release_irqrestore(&g_lock, f); return -EINVAL; }
    int slot = -1; for (int i = 0; i < SEM_MAX; i++) if (!g_sems[i].used) { slot = i; break; }
    if (slot < 0) { spinlock_release_irqrestore(&g_lock, f); return -ENOSPC; }
    memset(&g_sems[slot], 0, sizeof g_sems[slot]);
    g_sems[slot].used = 1; g_sems[slot].key = (int)key; g_sems[slot].id = g_next_id++; g_sems[slot].nsems = (int)nsems;
    int id = g_sems[slot].id;
    spinlock_release_irqrestore(&g_lock, f);
    return id;
}

int64_t sys_semop(uint64_t id, uint64_t sops_ptr, uint64_t nsops) {
    if (nsops == 0 || nsops > SEM_NSEMS_MAX) return -EINVAL;
    struct k_sembuf ops[SEM_NSEMS_MAX];
    if (!syscall_uptr_validate((void *)sops_ptr, nsops * sizeof(struct k_sembuf))) return -EFAULT;
    if (syscall_copy_from_user(ops, (void *)sops_ptr, nsops * sizeof(struct k_sembuf)) < 0) return -EFAULT;
    task_t *me = syscall_cur_task();

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&g_lock);
        sem_set_t *s = set_by_id((int)id);
        if (!s) { spinlock_release_irqrestore(&g_lock, f); return -EINVAL; }
        int ok = 1, nowait = 0;
        for (uint64_t i = 0; i < nsops; i++) {
            int n = ops[i].sem_num;
            if (n < 0 || n >= s->nsems) { spinlock_release_irqrestore(&g_lock, f); return -EFBIG; }
            int op = ops[i].sem_op;
            if (op < 0) { if (s->val[n] + op < 0) { ok = 0; if (ops[i].sem_flg & IPC_NOWAIT) nowait = 1; } }
            else if (op == 0) { if (s->val[n] != 0) { ok = 0; if (ops[i].sem_flg & IPC_NOWAIT) nowait = 1; } }
        }
        if (ok) {
            for (uint64_t i = 0; i < nsops; i++) s->val[ops[i].sem_num] += ops[i].sem_op;
            spinlock_release_irqrestore(&g_lock, f);
            return 0;
        }
        spinlock_release_irqrestore(&g_lock, f);
        if (nowait) return -EAGAIN;
        if (me && me->pending_kill) return -EINTR;
        task_sleep_ms(2);
    }
}

int64_t sys_semctl(uint64_t id, uint64_t num, uint64_t cmd, uint64_t arg) {
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    sem_set_t *s = set_by_id((int)id);
    if (!s) { spinlock_release_irqrestore(&g_lock, f); return -EINVAL; }
    int64_t r = 0;
    if (cmd == IPC_RMID) { s->used = 0; }
    else if (cmd == SETVAL) { if ((int)num >= 0 && (int)num < s->nsems) s->val[num] = (int)arg; else r = -EINVAL; }
    else if (cmd == GETVAL) { if ((int)num >= 0 && (int)num < s->nsems) r = s->val[num]; else r = -EINVAL; }
    spinlock_release_irqrestore(&g_lock, f);
    return r;
}
