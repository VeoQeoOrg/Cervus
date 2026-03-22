#include "../include/sched/sched.h"
#include "../include/sched/capabilities.h"
#include "../include/sched/spinlock.h"
#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../include/io/serial.h"
#include "../include/smp/smp.h"
#include "../include/smp/percpu.h"
#include "../include/apic/apic.h"
#include "../include/gdt/gdt.h"
#include "../include/fs/vfs.h"
#include "../include/panic/panic.h"
#include <string.h>
#include <stdlib.h>

#define KERNEL_STACK_PAGES (KERNEL_STACK_SIZE / 0x1000)
#define MAX_PIDS 4096

task_t* ready_queues[MAX_PRIORITY + 1] = {0};
task_t* current_task[MAX_CPUS] = {0};

static task_t* percpu_ready_queues[MAX_CPUS][MAX_PRIORITY + 1] = {0};
static task_t  idle_tasks[MAX_CPUS];
static task_t  bootstrap_tasks[MAX_CPUS];
static volatile uint64_t reschedule_calls = 0;
static spinlock_t ready_queue_lock = SPINLOCK_INIT;
static task_t*    pid_table[MAX_PIDS] = {0};
static uint32_t   next_pid = 1;
static spinlock_t pid_lock = SPINLOCK_INIT;

extern tss_t* tss[MAX_CPUS];

static inline void fix_gs_base(percpu_t* pc) {
    uint64_t val = (uint64_t)pc;
    asm volatile("wrmsr"
                 :: "c"(0xC0000101U),
                    "a"((uint32_t)val),
                    "d"((uint32_t)(val >> 32)));
}

uint32_t task_alloc_pid(void) {
    uint64_t _irqf;
    _irqf = spinlock_acquire_irqsave(&pid_lock);
    uint32_t found = 0;
    for (uint32_t attempt = 0; attempt < MAX_PIDS - 1; attempt++) {
        uint32_t i = next_pid;
        if (i == 0 || i >= MAX_PIDS) {
            next_pid = 1;
            i = 1;
        }
        next_pid = (next_pid + 1 >= MAX_PIDS) ? 1 : next_pid + 1;
        if (!pid_table[i]) { found = i; break; }
    }
    if (!found) {
        serial_printf("[PID] FATAL: pid table exhausted (MAX_PIDS=%u)!\n", MAX_PIDS);
    }
    spinlock_release_irqrestore(&pid_lock, _irqf);
    return found;
}

task_t* task_find_by_pid(uint32_t pid) {
    if (pid == 0 || pid >= MAX_PIDS) return NULL;
    return pid_table[pid];
}

static void pid_register(task_t* t) {
    if (t->pid && t->pid < MAX_PIDS) pid_table[t->pid] = t;
}

static void pid_unregister(task_t* t) {
    if (t->pid && t->pid < MAX_PIDS) pid_table[t->pid] = NULL;
}

static void idle_loop(void* arg);

#define STACK_CANARY_VALUE  0xDEADC0DEDEADC0DEULL
#define STACK_CANARY_PAGES  1

static uint64_t alloc_and_init_stack(task_t* t) {
    uintptr_t stack_virt = (uintptr_t)pmm_alloc_zero(KERNEL_STACK_PAGES);
    if (!stack_virt) return 0;
    t->stack_base = stack_virt;

    uint64_t* canary_page = (uint64_t*)stack_virt;
    for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++)
        canary_page[i] = STACK_CANARY_VALUE;

    uintptr_t stack_top = (stack_virt + KERNEL_STACK_SIZE) & ~0xFULL;
    uint64_t* sp = (uint64_t*)stack_top;

    if (t->flags & TASK_FLAG_FORK) {
        extern void task_trampoline_fork(void);
        *--sp = (uint64_t)task_trampoline_fork;
    } else if (t->is_userspace) {
        extern void task_trampoline_user(void);
        *--sp = (uint64_t)task_trampoline_user;
    } else {
        extern void task_trampoline(void);
        *--sp = (uint64_t)task_trampoline;
    }

    *--sp = (uint64_t)t;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;

    return (uint64_t)sp;
}

static void enqueue_global(task_t* t) {
    uint64_t f = spinlock_acquire_irqsave(&ready_queue_lock);
    t->next = ready_queues[t->priority];
    ready_queues[t->priority] = t;
    spinlock_release_irqrestore(&ready_queue_lock, f);
}

void __attribute__((used)) ctx_rsp_corruption_detected(task_t* old, uint64_t saved_rsp) {
    serial_printf("[CTX-CORRUPT] pid=%u rsp=0x%llx saved but INVALID (stack=0x%llx..0x%llx)!\n",
                  old ? old->pid : 0,
                  saved_rsp,
                  old ? old->stack_base : 0,
                  old ? (old->stack_base + KERNEL_STACK_SIZE) : 0);
    kernel_panic("context_switch: saved invalid RSP into task->rsp");
}

void sched_init(void) {
    memset(pid_table, 0, sizeof(pid_table));
    memset(bootstrap_tasks, 0, sizeof(bootstrap_tasks));
    next_pid = 1;
    for (uint32_t i = 0; i < smp_get_cpu_count(); i++) {
        task_t* idle = &idle_tasks[i];
        memset(idle, 0, sizeof(task_t));
        idle->priority        = 0;
        idle->runnable        = true;
        idle->state           = TASK_READY;
        idle->cpu_id          = i;
        idle->last_cpu        = i;
        idle->time_slice      = 1;
        idle->time_slice_init = 1;
        idle->entry           = idle_loop;
        idle->arg             = NULL;
        idle->is_userspace    = TASK_TYPE_KERNEL;
        idle->pid             = 0;
        idle->uid             = UID_ROOT;
        idle->gid             = GID_ROOT;
        idle->capabilities    = CAP_ALL;
        atomic_init_bool(&idle->on_cpu, false);
        idle->name[0]='i'; idle->name[1]='d';
        idle->name[2]='l'; idle->name[3]='e';
        idle->rsp = alloc_and_init_stack(idle);
        if (!idle->rsp) {
            kernel_panic("SCHED: failed to allocate idle stack");
        }
        current_task[i] = NULL;
        for (int p = 0; p <= MAX_PRIORITY; p++)
            percpu_ready_queues[i][p] = NULL;
    }
    serial_writestring("Scheduler initialized (PREEMPTIVE SMP MODE)\n");
}

task_t* task_create(const char* name, void (*entry)(void*), void* arg, int priority) {
    task_t* t = calloc(1, sizeof(task_t));
    if (!t) return NULL;
    t->pid             = task_alloc_pid();
    if (!t->pid) { free(t); return NULL; }
    t->ppid            = 0;
    t->uid             = UID_ROOT;
    t->gid             = GID_ROOT;
    t->capabilities    = CAP_ALL;
    t->entry           = entry;
    t->arg             = arg;
    t->priority        = priority > MAX_PRIORITY ? MAX_PRIORITY : priority;
    t->runnable        = true;
    t->state           = TASK_READY;
    t->cpu_id          = (uint32_t)-1;
    t->time_slice      = TASK_DEFAULT_TIMESLICE;
    t->time_slice_init = TASK_DEFAULT_TIMESLICE;
    t->rip             = (uint64_t)entry;
    t->is_userspace    = TASK_TYPE_KERNEL;
    atomic_init_bool(&t->on_cpu, false);
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->rsp = alloc_and_init_stack(t);
    if (!t->rsp) { free(t); return NULL; }
    t->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);
    pid_register(t);
    enqueue_global(t);
    serial_printf("[SCHED] task_create: '%s' pid=%u prio=%d\n", t->name, t->pid, t->priority);
    return t;
}

task_t* task_create_user(const char* name, uintptr_t entry, uintptr_t user_rsp, uint64_t cr3, int priority, vmm_pagemap_t* pagemap, uint32_t uid, uint32_t gid) {
    task_t* t = calloc(1, sizeof(task_t));
    if (!t) return NULL;
    t->pid             = task_alloc_pid();
    if (!t->pid) { free(t); return NULL; }
    t->ppid            = 0;
    t->uid             = uid;
    t->gid             = gid;
    t->capabilities    = cap_initial(uid);
    t->entry           = (void (*)(void*))entry;
    t->arg             = NULL;
    t->priority        = priority > MAX_PRIORITY ? MAX_PRIORITY : priority;
    t->runnable        = true;
    t->state           = TASK_READY;
    t->cpu_id          = (uint32_t)-1;
    t->time_slice      = TASK_DEFAULT_TIMESLICE;
    t->time_slice_init = TASK_DEFAULT_TIMESLICE;
    t->rip             = entry;
    t->is_userspace    = TASK_TYPE_USER;
    t->user_rsp        = user_rsp;
    t->cr3             = cr3;
    t->pagemap         = pagemap;
    t->brk_start       = 0;
    t->brk_current     = 0;
    t->brk_max         = 0x0000700000000000ULL;
    atomic_init_bool(&t->on_cpu, false);
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->rsp = alloc_and_init_stack(t);
    if (!t->rsp) { free(t); return NULL; }
    t->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);

    t->fd_table = fd_table_create();
    if (t->fd_table) {
        int stdio_ret = vfs_init_stdio(t);
        if (stdio_ret < 0)
            serial_printf("[SCHED] task_create_user: vfs_init_stdio failed: %d\n",
                          stdio_ret);
    }

    t->flags |= TASK_FLAG_OWN_PAGEMAP;

    pid_register(t);
    enqueue_global(t);
    serial_printf("[SCHED] task_create_user: '%s' pid=%u uid=%u entry=0x%llx user_rsp=0x%llx caps=0x%llx\n",
                  t->name, t->pid, t->uid, entry, user_rsp, t->capabilities);
    return t;
}

task_t* task_fork(task_t* parent) {
    if (!parent) return NULL;
    task_t* child = calloc(1, sizeof(task_t));
    if (!child) return NULL;
    child->pid = task_alloc_pid();
    if (!child->pid) { free(child); return NULL; }
    child->ppid            = parent->pid;
    child->priority        = parent->priority;
    child->is_userspace    = parent->is_userspace;
    strncpy(child->name, parent->name, sizeof(child->name)-1);
    child->uid             = parent->uid;
    child->gid             = parent->gid;
    child->capabilities    = parent->capabilities;
    child->time_slice      = parent->time_slice_init;
    child->time_slice_init = parent->time_slice_init;
    child->pagemap = vmm_clone_pagemap(parent->pagemap);
    if (!child->pagemap) { free(child); return NULL; }
    child->cr3 = (uint64_t)pmm_virt_to_phys(child->pagemap->pml4);
    child->brk_start   = parent->brk_start;
    child->brk_current = parent->brk_current;
    child->brk_max     = parent->brk_max;
    child->user_rsp = parent->user_rsp;
    serial_printf("[FORK-DBG2] parent pid=%u user_rsp=0x%llx user_saved_rip=0x%llx\n",
                  parent->pid, parent->user_rsp, parent->user_saved_rip);

    child->user_saved_rip = parent->user_saved_rip;
    child->user_saved_rbp = parent->user_saved_rbp;
    child->user_saved_rbx = parent->user_saved_rbx;
    child->user_saved_r12 = parent->user_saved_r12;
    child->user_saved_r13 = parent->user_saved_r13;
    child->user_saved_r14 = parent->user_saved_r14;
    child->user_saved_r15 = parent->user_saved_r15;
    child->user_saved_r11 = parent->user_saved_r11 | (1ULL << 9);

    child->flags |= TASK_FLAG_FORK;
    atomic_init_bool(&child->on_cpu, false);
    child->rsp = alloc_and_init_stack(child);
    if (!child->rsp) {
        vmm_free_pagemap(child->pagemap);
        free(child);
        return NULL;
    }
    vmm_sync_kernel_mappings(child->pagemap);
    serial_printf("[FORK-DBG] child pid=%u stack_base=0x%llx rsp=0x%llx\n",
                  child->pid, child->stack_base, child->rsp);
    child->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);
    if (child->fpu_state && parent->fpu_state) {
        memcpy(child->fpu_state, parent->fpu_state, sizeof(fpu_state_t));
        child->fpu_used = parent->fpu_used;
    }

    if (parent->fd_table)
        child->fd_table = fd_table_clone(parent->fd_table);

    child->state   = TASK_READY;
    child->runnable = true;
    child->parent  = parent;
    child->sibling = parent->children;
    parent->children = child;
    pid_register(child);

    serial_printf("[FORK-CHK] child=%p rsp=0x%llx stk=0x%llx rip=0x%llx\n",
                  (void*)child, child->rsp, child->stack_base, child->user_saved_rip);
    if ((uintptr_t)child < 0xffff800000000000ULL) {
        kernel_panic("FORK: child task_t ptr is physical (not HHDM)");
    }
    if (child->rsp < child->stack_base ||
        child->rsp > child->stack_base + KERNEL_STACK_SIZE) {
        kernel_panic("FORK: child rsp outside kernel stack bounds");
    }

    serial_printf("[SCHED] fork: parent='%s' pid=%u -> child pid=%u rip=0x%llx\n",
                  parent->name, parent->pid, child->pid, child->user_saved_rip);

    enqueue_global(child);
    return child;
}

void task_destroy(task_t* task) {
    if (!task) return;
    serial_printf("[DESTROY] pid=%u flags=0x%x on_cpu=%d\n",
                  task->pid, task->flags, (int)task->on_cpu._val);
    pid_unregister(task);
    if (task->fpu_state) {
        pmm_free(task->fpu_state, 1);
        task->fpu_state = NULL;
    }

    if (task->stack_base) {
        while (atomic_load_bool_acq(&task->on_cpu)) {
            asm volatile("pause" ::: "memory");
        }
        pmm_free((void*)task->stack_base, KERNEL_STACK_PAGES);
        task->stack_base = 0;
    }

    serial_printf("[DESTROY] pagemap\n");
    if (task->pagemap && (task->flags & (TASK_FLAG_FORK | TASK_FLAG_OWN_PAGEMAP))) {
        vmm_free_pagemap(task->pagemap);
        task->pagemap = NULL;
    }
    serial_printf("[DESTROY] fd_table\n");
    if (task->fd_table) {
        fd_table_destroy(task->fd_table);
        task->fd_table = NULL;
    }
    serial_printf("[DESTROY] free\n");
    free(task);
}

void task_reparent(task_t* child, task_t* new_parent) {
    if (!child || !new_parent) return;
    child->parent  = new_parent;
    child->ppid    = new_parent->pid;
    child->sibling = new_parent->children;
    new_parent->children = child;
}

void task_wakeup_waiters(uint32_t pid) {
    uint64_t _irqf;
    _irqf = spinlock_acquire_irqsave(&pid_lock);
    for (uint32_t i = 1; i < MAX_PIDS; i++) {
        task_t* t = pid_table[i];
        if (!t) continue;
        if (t->state != TASK_BLOCKED) continue;
        if (t->wait_for_pid != pid && t->wait_for_pid != (uint32_t)-1) continue;
        serial_printf("[SCHED] wakeup_waiters: waking pid=%u (waited for pid=%u) on_cpu=%d\n",
                      t->pid, pid, (int)t->on_cpu._val);
        t->wait_for_pid = 0;
        t->runnable     = true;
        t->state        = TASK_READY;
        spinlock_release_irqrestore(&pid_lock, _irqf);
        enqueue_global(t);
        _irqf = spinlock_acquire_irqsave(&pid_lock);
    }
    spinlock_release_irqrestore(&pid_lock, _irqf);
}

__attribute__((noreturn)) void task_exit(void) {
    uint32_t cpu = lapic_get_id();
    asm volatile("cli");

    percpu_t* pc = get_percpu();
    task_t* me = pc ? (task_t*)pc->current_task : current_task[cpu];

    if (pc) fix_gs_base(pc);

    serial_printf("[EXIT] task_exit called cpu=%u me=%p\n", cpu, (void*)me);

    if (!me) {
        uint64_t gs_base = 0;
        uint64_t kgs_base_lo = 0, kgs_base_hi = 0;
        asm volatile("rdgsbase %0" : "=r"(gs_base));
        asm volatile("mov $0xC0000102, %%ecx; rdmsr"
                     : "=a"(kgs_base_lo), "=d"(kgs_base_hi)
                     :: "ecx");
        uint64_t kgs_base = kgs_base_lo | ((uint64_t)kgs_base_hi << 32);

        serial_printf("[SCHED] task_exit: spurious call on cpu=%u (no current task)\n"
                      "  pc=0x%llx gs_base=0x%llx kgs_base=0x%llx\n"
                      "  current_task[%u]=%p gs:current_task=%p\n",
                      cpu,
                      (uint64_t)pc, gs_base, kgs_base,
                      cpu, current_task[cpu],
                      pc ? (void*)pc->current_task : (void*)0xDEAD);

        sched_reschedule();
        kernel_panic("task_exit: called with no current task");
    }

    task_t* init  = task_find_by_pid(1);
    task_t* child = me->children;
    me->children = NULL;
    while (child) {
        task_t* sib = child->sibling;
        if (init && init != me)
            task_reparent(child, init);
        child = sib;
    }

    serial_printf("[SCHED] task_exit: '%s' pid=%u exit=%d cpu=%u\n",
                  me->name, me->pid, me->exit_code, cpu);

    vmm_switch_pagemap(vmm_get_kernel_pagemap());

    me->runnable = false;
    me->state    = TASK_ZOMBIE;

    task_wakeup_waiters(me->pid);



    sched_reschedule();
    kernel_panic("task_exit: reschedule returned (should never happen)");
}

void task_kill(task_t* target) {
    if (!target) return;
    if (target->state == TASK_ZOMBIE) return;

    serial_printf("[KILL] task_kill pid=%u state=%d cpu=%u\n",
                  target->pid, (int)target->state, lapic_get_id());
    target->exit_code    = 130;
    target->pending_kill = true;

    if (target->state == TASK_BLOCKED) {
        target->wakeup_time_ns = 0;
        task_unblock(target);
    }

    if (!(target->flags & TASK_FLAG_STARTED)) {
        serial_printf("[KILL] pid=%u not yet started — skip IPI, pending_kill will be checked on first run\n",
                      target->pid);
        return;
    }

    for (uint32_t cpu = 0; cpu < smp_get_cpu_count(); cpu++) {
        if (current_task[cpu] == target) {
            extern uint32_t smp_get_lapic_id_for_cpu(uint32_t);
            ipi_reschedule_cpu(smp_get_lapic_id_for_cpu(cpu));
        }
    }
}

volatile uint32_t g_foreground_pid = 0;

void task_set_foreground(uint32_t pid) {
    g_foreground_pid = pid;
}

task_t* task_find_foreground(void) {
    uint32_t fpid = g_foreground_pid;
    if (fpid == 0) return NULL;
    task_t *t = task_find_by_pid(fpid);
    if (!t || t->state == TASK_ZOMBIE) {
        g_foreground_pid = 0;
        return NULL;
    }
    return t;
}

static task_t* sched_pick_next(uint32_t cpu) {
    uint64_t _irqf;
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        task_t** head = &percpu_ready_queues[cpu][p];
        task_t*  t    = *head;
        while (t) {
            if (t->runnable) {
                bool expected = false;
                if (atomic_cas_bool(&t->on_cpu, &expected, true)) {
                    *head   = t->next;
                    t->next = NULL;
                    return t;
                }
            }
            head = &t->next;
            t    = *head;
        }
    }

    _irqf = spinlock_acquire_irqsave(&ready_queue_lock);
    task_t* found = NULL;
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        task_t** head = &ready_queues[p];
        task_t*  t    = *head;
        while (t) {
            if (t->runnable) {
                bool expected = false;
                if (atomic_cas_bool(&t->on_cpu, &expected, true)) {
                    *head   = t->next;
                    t->next = NULL;
                    found   = t;
                    goto done_global;
                }
            }
            head = &t->next;
            t    = *head;
        }
    }
done_global:
    spinlock_release_irqrestore(&ready_queue_lock, _irqf);
    return found ? found : &idle_tasks[cpu];
}

void sched_reschedule(void) {
    uint64_t _irqf;
    asm volatile("cli");

    reschedule_calls++;
    uint32_t cpu  = lapic_get_id();

    task_t*  old  = current_task[cpu];
    task_t*  next = sched_pick_next(cpu);

    if (!next) { asm volatile("sti"); return; }



    if (old == next) {
        if (old == &idle_tasks[cpu]) {
            _irqf = spinlock_acquire_irqsave(&ready_queue_lock);
            task_t* found = NULL;
            for (int p = MAX_PRIORITY; p >= 0 && !found; p--) {
                task_t** head = &ready_queues[p];
                task_t*  t    = *head;
                while (t) {
                    if (t->runnable) {
                        bool expected = false;
                        if (atomic_cas_bool(&t->on_cpu, &expected, true)) {
                            *head   = t->next;
                            t->next = NULL;
                            found   = t;
                            break;
                        }
                    }
                    head = &t->next;
                    t    = *head;
                }
            }
            spinlock_release_irqrestore(&ready_queue_lock, _irqf);
            if (found) next = found;
            else        { asm volatile("sti"); return; }
        } else {
            old->time_slice = old->time_slice_init;
            asm volatile("sti");
            return;
        }
    }

    if (old && old->fpu_state && old->state != TASK_ZOMBIE) {
        fpu_save(old->fpu_state);
        old->fpu_used = true;
    }

    if (old && old != &idle_tasks[cpu]) {
        if (old->runnable) {
            old->time_slice = old->time_slice_init;
            old->last_cpu   = cpu;
            old->state      = TASK_READY;
            old->next = percpu_ready_queues[cpu][old->priority];
            percpu_ready_queues[cpu][old->priority] = old;
        }
    }

    uint64_t switch_cr3 = 0;
    if (next->cr3 && (!old || old->cr3 != next->cr3)) {
        if (next->pagemap)
            vmm_sync_kernel_mappings(next->pagemap);
        asm volatile("mfence" ::: "memory");
        switch_cr3 = next->cr3;
    } else if (!next->cr3) {
        vmm_pagemap_t* kpm = vmm_get_kernel_pagemap();
        if (kpm && kpm->pml4) {
            uint64_t kphys = (uint64_t)pmm_virt_to_phys(kpm->pml4);
            if (!old || old->cr3 != kphys)
                switch_cr3 = kphys;
        }
    }

    if (tss[cpu]) {
        if (next->is_userspace && next->stack_base == 0) {
            kernel_panic("SCHED: userspace task has stack_base=0");
        }
        tss[cpu]->rsp0 = next->stack_base + KERNEL_STACK_SIZE;
        percpu_t* pc = get_percpu();
        if (pc) {
            pc->syscall_kernel_rsp = tss[cpu]->rsp0;
            if (next->is_userspace) {
                pc->syscall_user_rsp = next->user_rsp;
            }
        }
    }

    next->cpu_id = cpu;
    next->state  = TASK_RUNNING;
    if (next->fpu_state) fpu_restore(next->fpu_state);

    if (!(next->flags & TASK_FLAG_STARTED)) {
        next->flags |= TASK_FLAG_STARTED;
        current_task[cpu] = next;
        if (next->cr3) {
        serial_printf("[SCHED] CR3 switch cpu=%u old=0x%llx -> new=0x%llx (pid %u->%u)\n", cpu, old ? old->cr3 : 0ULL, switch_cr3, old ? old->pid : 0, next->pid);
            asm volatile("mov %0, %%cr3" :: "r"(next->cr3) : "memory");
            switch_cr3 = 0;
        } else {
            asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
        }
        if (next->is_userspace) {
            serial_printf("[SCHED] CPU %u: first start '%s' pid=%u entry=0x%llx user_rsp=0x%llx\n",
                          cpu, next->name, next->pid,
                          (uint64_t)next->entry, next->user_rsp);
        }
    }

    {
        if ((uintptr_t)next < 0xffff800000000000ULL) {
            kernel_panic("SCHED: next task_t ptr is physical (not HHDM)");
        }
        if (next->stack_base && next->rsp != 0 &&
            (next->rsp < next->stack_base || next->rsp >= next->stack_base + KERNEL_STACK_SIZE)) {
            kernel_panic("SCHED: stack corruption — rsp outside kernel stack bounds");
        }
        if (old && old != &idle_tasks[cpu] && old != &bootstrap_tasks[cpu] &&
            old->stack_base && old->state != TASK_ZOMBIE) {
            uint64_t* canary = (uint64_t*)old->stack_base;
            for (int _ci = 0; _ci < 8; _ci++) {
                if (canary[_ci] != STACK_CANARY_VALUE) {
                    serial_printf("[CANARY] STACK OVERFLOW detected! pid=%u cpu=%u "
                                  "stack_base=0x%llx canary[%d]=0x%llx\n",
                                  old->pid, cpu, old->stack_base, _ci, canary[_ci]);
                    kernel_panic("SCHED: kernel stack overflow (canary corrupted)");
                }
            }
        }
        if (next->stack_base && (next->flags & TASK_FLAG_STARTED)) {
            uint64_t* canary = (uint64_t*)next->stack_base;
            for (int _ci = 0; _ci < 8; _ci++) {
                if (canary[_ci] != STACK_CANARY_VALUE) {
                    serial_printf("[CANARY] next task STACK CORRUPT! pid=%u cpu=%u "
                                  "stack_base=0x%llx canary[%d]=0x%llx\n",
                                  next->pid, cpu, next->stack_base, _ci, canary[_ci]);
                    kernel_panic("SCHED: next task kernel stack corrupted (canary)");
                }
            }
        }
    }
    if (old && old->stack_base && old->rsp != 0) {
        uintptr_t lo = old->stack_base;
        uintptr_t hi = old->stack_base + KERNEL_STACK_SIZE;
        if (old->rsp < lo || old->rsp >= hi) {
            serial_printf("[CTX-BUG] BEFORE switch: old pid=%u rsp=0x%llx OUTSIDE [0x%llx..0x%llx]!\n",
                          old->pid, old->rsp, lo, hi);
        }
    }


    if (old) context_switch(old, next, &current_task[cpu], switch_cr3);
    else     context_switch(&bootstrap_tasks[cpu], next, &current_task[cpu], switch_cr3);

    asm volatile("sti");
}

void task_yield(void) {
    sched_reschedule();
}

void sched_print_stats(void) {
    uint64_t _irqf;
    serial_printf("[SCHED] reschedule_calls=%llu\n", reschedule_calls);
    _irqf = spinlock_acquire_irqsave(&ready_queue_lock);
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        int n = 0;
        for (task_t* t = ready_queues[p]; t; t = t->next) n++;
        if (n) serial_printf("  prio %d: %d tasks\n", p, n);
    }
    spinlock_release_irqrestore(&ready_queue_lock, _irqf);
}

void task_unblock(task_t* t) {
    if (!t) return;
    t->runnable = true;
    t->state    = TASK_READY;
    enqueue_global(t);
}

void sched_wakeup_sleepers(uint64_t now_ns) {
    task_t* to_wake[MAX_PIDS];
    int     wake_count = 0;

    uint64_t _irqf = spinlock_acquire_irqsave(&pid_lock);
    for (uint32_t i = 1; i < MAX_PIDS; i++) {
        task_t *t = pid_table[i];
        if (!t) continue;
        if (t->state != TASK_BLOCKED) continue;
        if (t->wakeup_time_ns == 0) continue;
        if (now_ns >= t->wakeup_time_ns) {
            t->wakeup_time_ns = 0;
            t->runnable = true;
            t->state    = TASK_READY;
            to_wake[wake_count++] = t;
        }
    }
    spinlock_release_irqrestore(&pid_lock, _irqf);

    for (int i = 0; i < wake_count; i++) {
        enqueue_global(to_wake[i]);
    }
}

static void idle_loop(void* arg) {
    (void)arg;
    uint32_t cpu = lapic_get_id();
    serial_printf("[IDLE] CPU %u entering idle loop\n", cpu);
    while (1) asm volatile("sti; hlt");
}