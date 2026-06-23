#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/io/serial.h"

int64_t sys_wait(uint64_t pid_arg, uint64_t status_ptr, uint64_t flags)
{
    task_t *parent = syscall_cur_task();
    if (!parent) return -ESRCH;

retry:;
    task_t *zombie = NULL;
    bool zombie_still_on_cpu = false;
    {
        uint64_t _cf = spinlock_acquire_irqsave(&children_lock);
        task_t *child = parent->children;
        bool has_children = (child != NULL);
        while (child) {
            bool match = (pid_arg == (uint64_t)-1) || (child->pid == (uint32_t)pid_arg);
            if (match && child->state == TASK_ZOMBIE) {
                if (__atomic_load_n(&child->on_cpu._val, __ATOMIC_ACQUIRE)) {
                    zombie_still_on_cpu = true;
                } else {
                    zombie = child;
                    break;
                }
            }
            child = child->sibling;
        }
        spinlock_release_irqrestore(&children_lock, _cf);
        (void)has_children;
    }

    if (!zombie) {
        if (zombie_still_on_cpu) {
            task_yield();
            goto retry;
        }
        if (flags & WNOHANG) return 0;
        parent->wait_for_pid = (pid_arg == (uint64_t)-1) ? (uint32_t)-1 : (uint32_t)pid_arg;
        parent->runnable = false;
        parent->state    = TASK_BLOCKED;
        if (pid_arg != (uint64_t)-1)
            task_set_foreground((uint32_t)pid_arg);

        sched_reschedule();
        if (parent->pending_kill) return -EINTR;
        goto retry;
    }

    if (status_ptr) {
        int status = (zombie->exit_code & 0xFF) << 8;
        if (syscall_copy_to_user((void *)status_ptr, &status, sizeof(int)) < 0) return -EFAULT;
    }
    uint32_t zpid = zombie->pid;

    {
        uint64_t _cf = spinlock_acquire_irqsave(&children_lock);
        if (parent->children == zombie) {
            parent->children = zombie->sibling;
        } else {
            task_t *prev = parent->children;
            while (prev && prev->sibling != zombie) prev = prev->sibling;
            if (prev) prev->sibling = zombie->sibling;
        }
        zombie->sibling = NULL;
        zombie->parent  = NULL;
        spinlock_release_irqrestore(&children_lock, _cf);
    }

    extern void task_reassign_foreground(uint32_t dead_pid, task_t *parent);
    task_reassign_foreground(zpid, parent);
    task_destroy(zombie);
    return (int64_t)zpid;
}
