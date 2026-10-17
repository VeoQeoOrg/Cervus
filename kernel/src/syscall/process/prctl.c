#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/sched.h"
#include <string.h>

#define PR_SET_PDEATHSIG        1
#define PR_GET_PDEATHSIG        2
#define PR_GET_DUMPABLE         3
#define PR_SET_DUMPABLE         4
#define PR_GET_KEEPCAPS         7
#define PR_SET_KEEPCAPS         8
#define PR_SET_NAME            15
#define PR_GET_NAME            16
#define PR_GET_SECCOMP         21
#define PR_CAPBSET_READ        23
#define PR_GET_TIMERSLACK      30
#define PR_SET_TIMERSLACK      29
#define PR_SET_CHILD_SUBREAPER 36
#define PR_GET_CHILD_SUBREAPER 37
#define PR_SET_NO_NEW_PRIVS    38
#define PR_GET_NO_NEW_PRIVS    39
#define PR_GET_TID_ADDRESS     40
#define PR_SET_THP_DISABLE     41
#define PR_GET_THP_DISABLE     42
#define PR_SET_PTRACER         0x59616d61

static int64_t put_int(uint64_t uptr, int v)
{
    return syscall_copy_to_user((void *)uptr, &v, sizeof v) < 0 ? -EFAULT : 0;
}

int64_t sys_prctl(uint64_t option, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    task_t *t = syscall_cur_task();
    if (!t) return -EINVAL;

    switch (option) {
    case PR_SET_PDEATHSIG:
        if (a2 > 64) return -EINVAL;
        t->pdeathsig = (int32_t)a2;
        return 0;
    case PR_GET_PDEATHSIG:
        return put_int(a2, t->pdeathsig);
    case PR_GET_DUMPABLE:
        return (t->pr_flags & PR_FLAG_UNDUMPABLE) ? 0 : 1;
    case PR_SET_DUMPABLE:
        if (a2 > 1) return -EINVAL;
        if (a2) t->pr_flags &= ~PR_FLAG_UNDUMPABLE;
        else    t->pr_flags |= PR_FLAG_UNDUMPABLE;
        return 0;
    case PR_GET_KEEPCAPS:
        return 0;
    case PR_SET_KEEPCAPS:
        return a2 > 1 ? -EINVAL : 0;
    case PR_SET_NAME: {
        char name[16];
        memset(name, 0, sizeof name);
        for (int i = 0; i < 15; i++) {
            if (syscall_copy_from_user(&name[i], (const void *)(a2 + (uint64_t)i), 1) < 0)
                return -EFAULT;
            if (!name[i]) break;
        }
        memset(t->name, 0, sizeof t->name);
        memcpy(t->name, name, sizeof name);
        return 0;
    }
    case PR_GET_NAME: {
        char name[16];
        memset(name, 0, sizeof name);
        strncpy(name, t->name, 15);
        return syscall_copy_to_user((void *)a2, name, sizeof name) < 0 ? -EFAULT : 0;
    }
    case PR_GET_SECCOMP:
        return 0;
    case PR_CAPBSET_READ:
        return a2 < 64 ? 1 : -EINVAL;
    case PR_GET_TIMERSLACK:
        return 50000;
    case PR_SET_TIMERSLACK:
        return 0;
    case PR_SET_CHILD_SUBREAPER:
        if (a2) t->pr_flags |= PR_FLAG_SUBREAPER;
        else    t->pr_flags &= ~PR_FLAG_SUBREAPER;
        return 0;
    case PR_GET_CHILD_SUBREAPER:
        return put_int(a2, (t->pr_flags & PR_FLAG_SUBREAPER) ? 1 : 0);
    case PR_SET_NO_NEW_PRIVS:
        if (a2 != 1 || a3 || a4 || a5) return -EINVAL;
        t->pr_flags |= PR_FLAG_NO_NEW_PRIVS;
        return 0;
    case PR_GET_NO_NEW_PRIVS:
        if (a2 || a3 || a4 || a5) return -EINVAL;
        return (t->pr_flags & PR_FLAG_NO_NEW_PRIVS) ? 1 : 0;
    case PR_GET_TID_ADDRESS:
        return syscall_copy_to_user((void *)a2, &t->clear_tid, sizeof(uint64_t)) < 0 ? -EFAULT : 0;
    case PR_GET_THP_DISABLE:
        return 0;
    case PR_SET_THP_DISABLE:
        return 0;
    case PR_SET_PTRACER:
        return 0;
    default:
        return -EINVAL;
    }
}
