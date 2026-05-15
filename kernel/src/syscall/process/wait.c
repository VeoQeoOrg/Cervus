#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/io/serial.h"

int64_t sys_wait(uint64_t pid_arg, uint64_t status_ptr, uint64_t flags)
{
    task_t *parent = syscall_cur_task();
    if (!parent) return -ESRCH;

retry:;
    task_t *zombie = NULL;
    {
        uint64_t _cf = spinlock_acquire_irqsave(&children_lock);
        task_t *child = parent->children;
        bool has_children = (child != NULL);
        while (child) {
            bool match = (pid_arg == (uint64_t)-1) || (child->pid == (uint32_t)pid_arg);
            if (match && child->state == TASK_ZOMBIE) { zombie = child; break; }
            child = child->sibling;
        }
        spinlock_release_irqrestore(&children_lock, _cf);
        (void)has_children;
    }

    if (!zombie) {
        if (flags & WNOHANG) return 0;
        syscall_save_user_regs(parent);
        parent->wait_for_pid = (pid_arg == (uint64_t)-1) ? (uint32_t)-1 : (uint32_t)pid_arg;
        parent->runnable = false;
        parent->state    = TASK_BLOCKED;
        if (pid_arg != (uint64_t)-1)
            task_set_foreground((uint32_t)pid_arg);

        serial_printf("[WAIT] pid=%u blocking: user_rsp=0x%llx task_rsp=0x%llx\n",
                      parent->pid, parent->user_rsp, parent->rsp);
        sched_reschedule();
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

    serial_printf("[SYSCALL] wait: parent pid=%u reaped child pid=%u\n",
                  parent->pid, zpid);

    if (g_foreground_pid == zpid)
        task_set_foreground(0);
    task_destroy(zombie);
    serial_printf("[SYSCALL] wait: task_destroy done, returning %u\n", zpid);
    serial_printf("[WAIT-RET] parent pid=%u user_saved_rip=0x%llx user_rsp=0x%llx\n",
                  parent->pid, parent->user_saved_rip, parent->user_rsp);
    return (int64_t)zpid;
}
